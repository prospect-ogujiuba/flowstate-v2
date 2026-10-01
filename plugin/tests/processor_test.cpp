// Processor-level tests: the real FlowstateProcessor, driven without a host. Covers host sync,
// capture, MIDI pass-through, the bridge, state save/restore and editor lifetime (P1-6).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <fstream>
#include <sstream>
#include <thread>

using namespace flowstate::plugin;
using nlohmann::json;

namespace {

json loadScore(const char* name) {
    std::ifstream in(std::string(FLOWSTATE_CORE_FIXTURES_DIR) + "/" + name);
    REQUIRE(in.good());
    std::stringstream text;
    text << in.rdbuf();
    return json::parse(text.str());
}

struct FakePlayHead final : juce::AudioPlayHead {
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setIsPlaying(playing);
        p.setPpqPosition(ppq);
        p.setBpm(bpm);
        p.setTimeSignature(TimeSignature{num, den});
        p.setIsLooping(looping);
        p.setLoopPoints(LoopPoints{8.0, 24.0});
        return p;
    }
    bool playing = true, looping = true;
    double ppq = 17.5, bpm = 96.0;
    int num = 4, den = 4;
};

void run(FlowstateProcessor& p, juce::MidiBuffer& midi, int blocks = 1, int blockSize = 512) {
    juce::AudioBuffer<float> audio(2, blockSize);
    for (int i = 0; i < blocks; ++i) p.processBlock(audio, midi);
}

std::string stateOf(FlowstateProcessor& p) {
    juce::MemoryBlock block;
    p.getStateInformation(block);
    return block.toString().toStdString();
}

json command(FlowstateProcessor& p, const json& c) { return json::parse(p.handleBridgeCommand(c.dump())); }

}  // namespace

TEST_CASE("host sync: the playhead reaches the transport event and the effective context") {
    FlowstateProcessor p;
    p.prepareToPlay(48000.0, 512);
    FakePlayHead head;
    p.setPlayHead(&head);
    juce::MidiBuffer midi;
    run(p, midi);

    const auto t = json::parse(p.transportEventJson());
    CHECK(t["type"] == "transport");
    CHECK(t["transport"]["playing"] == true);
    CHECK(t["transport"]["tempo"] == 96.0);
    CHECK(t["transport"]["bar"] == 5);
    CHECK(t["transport"]["loop"]["startPpq"] == 8.0);

    auto s = json::parse(p.sessionEventJson());
    CHECK(s["session"]["context"]["timeFrom"] == "host");
    CHECK(s["session"]["context"]["tempo"] == 96.0);

    // Overrides win over the host.
    const auto r = command(p, {{"type", "setContextOverride"},
                               {"override", {{"tonic", "Eb"}, {"mode", "dorian"}, {"tempo", 140.0}, {"meterNumerator", nullptr},
                                             {"meterDenominator", nullptr}, {"bars", nullptr}, {"swing", nullptr}}}});
    CHECK(r["ok"] == true);
    CHECK(r["session"]["context"]["tempo"] == 140.0);
    CHECK(r["session"]["context"]["timeFrom"] == "override");
    CHECK(r["session"]["context"]["keyFrom"] == "override");
    p.setPlayHead(nullptr);
}

TEST_CASE("capture: incoming notes are recorded and passed through untouched") {
    FlowstateProcessor p;
    p.prepareToPlay(48000.0, 512);
    FakePlayHead head;
    p.setPlayHead(&head);

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 10);
    midi.addEvent(juce::MidiMessage::noteOff(1, 60), 300);
    run(p, midi);
    CHECK(midi.getNumEvents() == 2);  // pass-through

    p.pumpCapture();
    auto s = json::parse(p.sessionEventJson());
    CHECK(s["session"]["captureBars"] == 1);

    // Two more bars of silence at 96 bpm: the capture window grows with the clock.
    juce::MidiBuffer empty;
    run(p, empty, static_cast<int>(48000.0 * 60.0 / 96.0 * 4 * 2 / 512) + 1);
    p.pumpCapture();
    s = json::parse(p.sessionEventJson());
    CHECK(s["session"]["captureBars"] == 3);
    p.setPlayHead(nullptr);
}

TEST_CASE("state: a project reopens with its ideas intact") {
    std::string saved;
    std::string current;
    {
        FlowstateProcessor p;
        auto& s = p.getSession();
        s.addNode(loadScore("example.json"), fb::NodeKind::Initial, "lofi keys", std::nullopt, 42, 1000);
        s.addNode(loadScore("six_eight.json"), fb::NodeKind::Edit, "six eight", std::nullopt, 7, 2000);
        // Commands go through the bridge so the processor snapshots its state.
        const auto r = command(p, {{"type", "undo"}});
        REQUIRE(r["ok"] == true);
        current = r["session"]["currentNodeId"];
        p.setEditorSize(1111, 777);
        saved = stateOf(p);
    }

    FlowstateProcessor reopened;
    reopened.setStateInformation(saved.data(), static_cast<int>(saved.size()));
    CHECK(stateOf(reopened) == saved);
    const auto s = json::parse(reopened.sessionEventJson())["session"];
    CHECK(s["nodes"].size() == 2);
    CHECK(s["currentNodeId"] == current);
    CHECK(s["canRedo"] == true);
    CHECK_FALSE(s["clip"].is_null());
    CHECK(reopened.getEditorSize() == juce::Point<int>(1111, 777));
}

TEST_CASE("state: set from another thread is visible at once and applied on the message thread") {
    FlowstateProcessor source;
    source.getSession().addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 1);
    command(source, {{"type", "setPreviewSynth"}, {"enabled", false}});
    const auto saved = stateOf(source);

    FlowstateProcessor target;
    std::thread host([&] { target.setStateInformation(saved.data(), static_cast<int>(saved.size())); });
    host.join();
    CHECK(stateOf(target) == saved);  // before the async update runs

    // Any bridge command applies a pending restore first.
    const auto r = command(target, {{"type", "hello"}, {"protocol", "flowstate.bridge.v0"}});
    CHECK(r["session"]["nodes"].size() == 1);
    CHECK(r["session"]["settings"]["previewSynth"] == false);
}

TEST_CASE("state: foreign or corrupt blobs are ignored") {
    FlowstateProcessor p;
    p.getSession().addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 1);
    command(p, {{"type", "setPreviewSynth"}, {"enabled", true}});
    const auto before = stateOf(p);
    const std::string junk = "<?xml version=\"1.0\"?><FlowstateSpike/>";
    p.setStateInformation(junk.data(), static_cast<int>(junk.size()));
    p.setStateInformation(nullptr, 0);
    CHECK(stateOf(p) == before);
}

TEST_CASE("editor: closing and reopening loses nothing") {
    FlowstateProcessor p;
    p.getSession().addNode(loadScore("example.json"), fb::NodeKind::Initial, "first", std::nullopt, 3, 1);
    command(p, {{"type", "setMidiOut"}, {"midiOut", {{"role", "bass"}, {"channel", nullptr}}}});
    const auto before = stateOf(p);

    for (int i = 0; i < 3; ++i) {
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        REQUIRE(editor != nullptr);
        CHECK(editor->getWidth() >= 720);
        editor.reset();
    }
    CHECK(stateOf(p) == before);

    // Without an editor, drag reports that it needs one instead of failing silently.
    const auto r = command(p, {{"type", "startDrag"}, {"nodeId", nullptr}, {"partIds", nullptr}, {"splitDrums", false}});
    CHECK(r["error"]["code"] == "unavailable");
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juce;  // the test thread is the message thread
    doctest::Context context(argc, argv);
    return context.run();
}

TEST_CASE("library: the bundled catalog is searchable and a clip becomes an idea") {
    FlowstateProcessor p;
    const json query = {{"text", ""}, {"origins", {"library"}}, {"roles", nullptr}, {"genres", nullptr},
                        {"fitContext", false}, {"limit", 100}, {"offset", 0}};
    auto r = command(p, {{"type", "searchCatalog"}, {"query", query}});
    REQUIRE(r["ok"] == true);
    CHECK(r["catalog"]["total"] == 29);
    for (const auto& e : r["catalog"]["entries"]) CHECK(e["credit"]["text"] == "MIDI by GodFlow (flowknows) for Flowstate.");

    r = command(p, {{"type", "useEntry"}, {"entryId", "lib:godflow/bass-05"}});
    REQUIRE(r["ok"] == true);
    CHECK(r["session"]["nodes"].back()["kind"] == "library");
    CHECK(r["session"]["clip"]["parts"][0]["role"] == "bass");
}
