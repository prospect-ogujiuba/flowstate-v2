// Loads the built Flowstate VST3s through JUCE's VST3 host, as a DAW would: MIDI pass-through
// under a moving transport, silence with no idea, bypass, a saved session (two ideas, realized)
// surviving into a fresh instance, the restored idea playing in time (MIDI out, and the preview
// synth on the instrument), and part filtering: "send: drums only", forced onto one channel.
// Usage: flowstate_host_smoke <Flowstate.vst3> [<Flowstate MIDI FX.vst3>]

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>

namespace {

int failures = 0;

#define EXPECT(cond, ...)                          \
    do {                                           \
        if (!(cond)) {                             \
            ++failures;                            \
            std::printf("  FAIL: %s -- ", #cond);  \
            std::printf(__VA_ARGS__);              \
            std::printf("\n");                     \
        }                                          \
    } while (0)

struct FakePlayHead final : juce::AudioPlayHead {
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setIsPlaying(playing);
        p.setPpqPosition(ppq);
        p.setBpm(bpm);
        p.setTimeSignature(TimeSignature{4, 4});
        return p;
    }
    bool playing = true;
    double ppq = 0.0, bpm = 100.0;
};

std::unique_ptr<juce::AudioPluginInstance> load(const juce::String& path, double sr, int bs) {
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile(types, path);
    if (types.isEmpty()) {
        std::printf("  no plugin types found in %s\n", path.toRawUTF8());
        return {};
    }
    juce::String error;
    auto instance = format.createInstanceFromDescription(*types[0], sr, bs, error);
    if (instance == nullptr) std::printf("  load failed: %s\n", error.toRawUTF8());
    return instance;
}

juce::String fixture(const char* name) {
    return juce::File(FLOWSTATE_CORE_FIXTURES_DIR).getChildFile(name).loadFileAsString();
}

// A saved Flowstate state with two ideas, the second current, built by hand in the documented
// envelope (plugin/src/session/PluginState.h) so this test doesn't link the plugin's code.
juce::String savedState(const char* midiOut = R"({"role":null,"channel":null})") {
    const auto node = [](const char* id, const char* parent, const juce::String& score, int seed) {
        return juce::String(R"({"id":")") + id + R"(","parentId":)" + (parent ? juce::String("\"") + parent + "\"" : "null") +
               R"(,"kind":"initial","prompt":null,"partIds":null,"createdAtMs":1,"rating":null,"seed":)" + juce::String(seed) +
               R"(,"score":)" + score + "}";
    };
    const auto overrideJson = R"({"tonic":null,"mode":null,"tempo":null,"meterNumerator":null,"meterDenominator":null,"bars":null,"swing":null})";
    return juce::String(R"({"format":"flowstate.plugin.state","version":1,"session":{"protocol":"flowstate.bridge.v0",)") +
           R"("instanceId":"smoke","override":)" + overrideJson + R"(,"nodes":[)" + node("n1", nullptr, fixture("example.json"), 1) +
           "," + node("n2", "n1", fixture("seven_eight.json"), 2) +
           R"(],"currentNodeId":"n2","redo":[],"thread":[],"parts":[],"audition":{"nodeId":null,"loop":null,"freeRun":false},)" +
           R"("midiOut":)" + midiOut + R"(,"previewSynth":true},"editor":{"width":960,"height":600}})";
}

// A hosted VST3's state is the host's XML ("VST3PluginState" with a base64 "IComponent" child)
// around the plugin's own data, which the plugin wrapper follows with JUCE private data.
juce::MemoryBlock wrapForHost(const juce::String& pluginState) {
    juce::XmlElement xml("VST3PluginState");
    juce::MemoryBlock data(pluginState.toRawUTF8(), pluginState.getNumBytesAsUTF8());
    xml.createNewChildElement("IComponent")->addTextElement(data.toBase64Encoding());
    juce::MemoryBlock out;
    juce::AudioProcessor::copyXmlToBinary(xml, out);
    return out;
}

juce::var pluginStateOf(const juce::MemoryBlock& hostState) {
    const auto xml = juce::AudioProcessor::getXmlFromBinary(hostState.getData(), (int) hostState.getSize());
    if (xml == nullptr || xml->getChildByName("IComponent") == nullptr) return {};
    juce::MemoryBlock data;
    if (!data.fromBase64Encoding(xml->getChildByName("IComponent")->getAllSubText())) return {};
    // Our JSON envelope ends with the editor object; JUCE's private data follows it.
    const auto text = data.toString();
    const auto end = text.indexOf("\"editor\":");
    const auto close = end < 0 ? -1 : text.indexOf(end, "}}");
    return close < 0 ? juce::var() : juce::JSON::parse(text.substring(0, close + 2));
}

void runVariant(const juce::String& path, bool instrument) {
    std::printf("%s\n", path.toRawUTF8());
    constexpr double sr = 48000.0;
    constexpr int bs = 480;

    auto plugin = load(path, sr, bs);
    EXPECT(plugin != nullptr, "plugin loads");
    if (plugin == nullptr) return;
    EXPECT(plugin->acceptsMidi() && plugin->producesMidi(), "MIDI in and out");
    // JUCE's VST3 host doesn't report isMidiEffect (only its AU host does); the MIDI FX has no audio out.
    EXPECT((plugin->getTotalNumOutputChannels() > 0) == instrument, "variant type (%d outputs)",
           plugin->getTotalNumOutputChannels());

    FakePlayHead head;
    plugin->setPlayHead(&head);
    plugin->prepareToPlay(sr, bs);

    juce::AudioBuffer<float> audio(juce::jmax(2, plugin->getTotalNumOutputChannels()), bs);
    juce::MidiBuffer midi;
    int passed = 0;
    float peak = 0.0f;
    const auto samplesPerBeat = sr * 60.0 / head.bpm;
    for (int i = 0; i < 400; ++i) {
        audio.clear();
        midi.clear();
        if (i % 10 == 0) midi.addEvent(juce::MidiMessage::noteOn(1, 60 + i % 12, (juce::uint8) 90), 5);
        if (i % 10 == 5) midi.addEvent(juce::MidiMessage::noteOff(1, 60 + (i - 5) % 12), 5);
        const auto sent = midi.getNumEvents();
        plugin->processBlock(audio, midi);
        passed += midi.getNumEvents() == sent ? sent : 0;
        peak = juce::jmax(peak, audio.getMagnitude(0, bs));
        head.ppq += bs / samplesPerBeat;
    }
    EXPECT(passed == 80, "played MIDI passes through (%d of 80 events)", passed);
    EXPECT(peak == 0.0f, "silent with no idea (peak %.4f)", peak);

    // Bypassed: MIDI still passes, audio stays silent.
    midi.clear();
    midi.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 90), 0);
    plugin->processBlockBypassed(audio, midi);
    EXPECT(midi.getNumEvents() == 1, "bypass passes MIDI through");

    // A project with two ideas reopens intact, and the state is stable across instances.
    const auto blob = wrapForHost(savedState());
    plugin->setStateInformation(blob.getData(), (int) blob.getSize());
    juce::MemoryBlock state;
    plugin->getStateInformation(state);
    const auto parsed = pluginStateOf(state);
    EXPECT(parsed["session"]["nodes"].size() == 2, "both ideas restored");
    EXPECT(parsed["session"]["currentNodeId"].toString() == "n2", "selection restored");

    auto second = load(path, sr, bs);
    if (second != nullptr) {
        second->setStateInformation(state.getData(), (int) state.getSize());
        juce::MemoryBlock state2;
        second->getStateInformation(state2);
        EXPECT(state == state2, "state round trip is stable");
    }

    // The restored idea plays in time with the host: every part on its own channel, drums on 10.
    const auto playFor = [&](int blocks, std::set<int>& channels, float& loudest) {
        int ons = 0;
        for (int i = 0; i < blocks; ++i) {
            audio.clear();
            midi.clear();
            plugin->processBlock(audio, midi);
            for (const auto m : midi)
                if (m.getMessage().isNoteOn()) {
                    ++ons;
                    channels.insert(m.getMessage().getChannel());
                }
            loudest = juce::jmax(loudest, audio.getMagnitude(0, bs));
            head.ppq += bs / samplesPerBeat;
        }
        return ons;
    };
    head.ppq = 0.0;
    std::set<int> channels;
    float loudest = 0.0f;
    const auto ons = playFor(800, channels, loudest);
    EXPECT(ons > 0, "the idea plays (%d note-ons)", ons);
    EXPECT(channels.size() == 4 && channels.count(10) == 1, "all four parts on their channels (%d channels)",
           (int) channels.size());
    EXPECT((loudest > 0.0f) == instrument, "preview synth on the instrument only (peak %.4f)", loudest);

    // Stop between the two runs, as a user would.
    head.playing = false;
    playFor(2, channels, loudest);
    head.playing = true;

    // Part filtering: "send: drums only", forced onto channel 7.
    const auto drumsOnly = wrapForHost(savedState(R"({"role":"drums","channel":7})"));
    plugin->setStateInformation(drumsOnly.getData(), (int) drumsOnly.getSize());
    head.ppq = 0.0;
    std::set<int> filtered;
    const auto drumOns = playFor(800, filtered, loudest);
    EXPECT(drumOns > 0 && drumOns < ons, "drums only (%d of %d note-ons)", drumOns, ons);
    EXPECT(filtered == std::set<int>{7}, "forced onto channel 7 (%d channels)", (int) filtered.size());
    plugin->setStateInformation(blob.getData(), (int) blob.getSize());

    // Garbage never replaces a good session. (Compared with the state just before: the host's
    // own bypass flag, in JUCE's part of the blob, follows the last processBlock call.)
    juce::MemoryBlock good;
    plugin->getStateInformation(good);
    EXPECT(pluginStateOf(good)["session"]["midiOut"]["role"].isVoid(), "the two-idea project is back");
    const auto junk = wrapForHost("not a state");
    plugin->setStateInformation(junk.getData(), (int) junk.getSize());
    juce::MemoryBlock state3;
    plugin->getStateInformation(state3);
    EXPECT(state3 == good, "junk state ignored");

    plugin->releaseResources();
}

}  // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI juce;
    if (argc < 2) {
        std::printf("usage: %s <instrument.vst3> [<midifx.vst3>]\n", argv[0]);
        return 2;
    }
    runVariant(argv[1], true);
    if (argc > 2) runVariant(argv[2], false);
    std::printf("%s (%d failures)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
