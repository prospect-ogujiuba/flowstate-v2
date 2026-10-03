// Processor-level tests: the real FlowstateProcessor, driven without a host. Covers host sync,
// capture, MIDI pass-through, the bridge, state save/restore and editor lifetime (P1-6).
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "OsKeyStore.h"
#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
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

// ---- The agent service client against a fake service ----------------------------------------------

namespace {

// A one-shot HTTP server on localhost that answers POST /v1/plan with a chunked SSE stream: the
// given events (with a keepalive comment first), then either the end of the stream or, with
// `holdOpen`, nothing until the client hangs up.
class FakeService {
public:
    FakeService(std::vector<json> events, bool holdOpen = false, int status = 200)
        : events_(std::move(events)), holdOpen_(holdOpen), status_(status) {
        REQUIRE(listener_.createListener(0, "127.0.0.1"));
        port_ = listener_.getBoundPort();
        thread_ = std::thread([this] { serve(); });
    }
    ~FakeService() {
        listener_.close();
        thread_.join();
    }
    std::string url() const { return "http://127.0.0.1:" + std::to_string(port_); }
    std::string request() {
        const std::lock_guard lock(mutex_);
        return request_;
    }
    bool clientHungUp() const { return hungUp_; }
    // GET /v1/health answers with this `byok` flag, and doesn't count as the one streamed request.
    std::atomic<bool> byok{true};
    std::atomic<int> healthChecks{0};

private:
    void serve() {
        std::unique_ptr<juce::StreamingSocket> client;
        std::string text;
        char buffer[4096];
        const auto send = [&](const std::string& data) { return client->write(data.data(), static_cast<int>(data.size())) == static_cast<int>(data.size()); };
        while (true) {
            client.reset(listener_.waitForNextConnection());
            if (client == nullptr) return;
            text.clear();
            while (true) {
                const auto headerEnd = text.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    const auto lengthAt = juce::String(text.substr(0, headerEnd)).toLowerCase().indexOf("content-length:");
                    const auto length = lengthAt < 0 ? 0 : juce::String(text.substr(static_cast<size_t>(lengthAt) + 15)).getIntValue();
                    if (text.size() >= headerEnd + 4 + static_cast<size_t>(length)) break;
                }
                // A non-blocking read returns at once when nothing has arrived yet, so wait first.
                if (client->waitUntilReady(true, 5000) != 1) return;
                const int n = client->read(buffer, sizeof buffer, false);
                if (n <= 0) return;
                text.append(buffer, static_cast<size_t>(n));
            }
            if (text.rfind("GET /v1/health", 0) != 0) break;
            const auto body = json{{"protocol", "flowstate.bridge.v0"}, {"version", "test"}, {"ok", true}, {"features", {{"byok", byok.load()}}}}.dump();
            send("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(body.size()) +
                 "\r\nConnection: close\r\n\r\n" + body);
            ++healthChecks;
        }
        {
            const std::lock_guard lock(mutex_);
            request_ = text;
        }
        const auto chunk = [&](const std::string& data) {
            std::ostringstream hex;
            hex << std::hex << data.size();
            return send(hex.str() + "\r\n" + data + "\r\n");
        };
        send("HTTP/1.1 " + std::to_string(status_) + " OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\n\r\n");
        chunk(": keepalive\n\n");
        for (const auto& e : events_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            chunk("data: " + e.dump() + "\n\n");
        }
        if (holdOpen_) {
            // Wait for the client to abort the request.
            for (int i = 0; i < 500 && !hungUp_; ++i) {
                if (client->waitUntilReady(true, 20) == 1 && client->read(buffer, sizeof buffer, false) <= 0) hungUp_ = true;
            }
            return;
        }
        send("0\r\n\r\n");
    }

    juce::StreamingSocket listener_;
    int port_ = 0;
    std::vector<json> events_;
    bool holdOpen_;
    int status_;
    std::thread thread_;
    std::mutex mutex_;
    std::string request_;
    std::atomic<bool> hungUp_{false};
};

struct ServiceUrl {
    explicit ServiceUrl(const std::string& url) {
#if JUCE_WINDOWS
        _putenv_s("FLOWSTATE_SERVICE_URL", url.c_str());
#else
        setenv("FLOWSTATE_SERVICE_URL", url.c_str(), 1);
#endif
    }
    ~ServiceUrl() {
#if JUCE_WINDOWS
        _putenv_s("FLOWSTATE_SERVICE_URL", "");
#else
        unsetenv("FLOWSTATE_SERVICE_URL");
#endif
    }
};

// Plays the host and pumps the message thread until `done` or the timeout; returns the MIDI that came out.
struct Played {
    int noteOns = 0;
    std::set<int> channels;
    float peak = 0.0f;
};
Played play(FlowstateProcessor& p, FakePlayHead& head, const std::function<bool()>& done, int timeoutMs = 10000) {
    Played out;
    juce::AudioBuffer<float> audio(2, 480);
    juce::MidiBuffer midi;
    const auto start = juce::Time::getMillisecondCounter();
    while (!done() && juce::Time::getMillisecondCounter() - start < static_cast<juce::uint32>(timeoutMs)) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        for (int i = 0; i < 10; ++i) {
            midi.clear();
            p.processBlock(audio, midi);
            for (const auto m : midi)
                if (m.getMessage().isNoteOn()) {
                    ++out.noteOns;
                    out.channels.insert(m.getMessage().getChannel());
                }
            out.peak = std::max(out.peak, audio.getMagnitude(0, 0, audio.getNumSamples()));
            head.ppq += 480.0 / (48000.0 * 60.0 / head.bpm);
        }
    }
    return out;
}

std::vector<json> streamOf(const json& score) {
    auto header = score;
    header["parts"] = json::array();
    std::vector<json> events{{{"type", "header"}, {"score", header}}};
    for (const auto& part : score["parts"]) {
        events.push_back({{"type", "partStarted"}, {"partId", part["id"]}, {"role", part["role"]}});
        events.push_back({{"type", "partDone"}, {"part", part}});
    }
    events.push_back({{"type", "done"}, {"score", score}});
    return events;
}

}  // namespace

TEST_CASE("service: generate streams parts from the service into an idea that plays in time") {
    const auto score = loadScore("example.json");
    FakeService service(streamOf(score));
    ServiceUrl url(service.url());

    FlowstateProcessor p;
    p.prepareToPlay(48000.0, 480);
    FakePlayHead head;
    head.looping = false;
    head.ppq = 0.0;
    head.bpm = 120.0;
    p.setPlayHead(&head);
    std::vector<json> events;
    p.setEventListener([&](const std::string& e) { events.push_back(json::parse(e)); });

    const auto r = command(p, {{"type", "generate"}, {"prompt", "late night keys"}, {"roles", nullptr}, {"count", 1}, {"capture", nullptr}});
    REQUIRE(r["ok"] == true);
    const auto finished = [&] {
        return !events.empty() && (events.back()["type"] == "generationDone" || events.back()["type"] == "generationFailed");
    };
    const auto played = play(p, head, finished);
    REQUIRE(finished());
    CHECK(events.back()["type"] == "generationDone");
    int partsReady = 0;
    for (const auto& e : events) partsReady += e["type"] == "partReady" ? 1 : 0;
    CHECK(partsReady == 4);

    // The request the service saw: a valid PlanRequest with the session's context, and a request id.
    const auto request = service.request();
    CHECK(request.rfind("POST /v1/plan", 0) == 0);
    CHECK(juce::String(request).containsIgnoreCase("x-flowstate-request-id:"));
    fb::PlanRequest plan;
    fb::from_json(json::parse(request.substr(request.find("\r\n\r\n") + 4)), plan);
    CHECK(plan.prompt == "late night keys");
    CHECK(plan.context.tempo == 120.0);

    const auto session = json::parse(p.sessionEventJson())["session"];
    CHECK(session["nodes"].size() == 1);
    CHECK(session["generations"].empty());
    CHECK(session["clip"]["parts"].size() == 4);

    // It plays: MIDI out and the preview synth, while the host rolls.
    const auto more = play(p, head, [] { return false; }, 300);
    CHECK(played.noteOns + more.noteOns > 0);
    CHECK(more.channels.count(10) == 1);  // drums on their channel
    CHECK(std::max(played.peak, more.peak) > 0.0f);

    // MIDI out "bass only", forced onto channel 5: one channel, and the preview follows the filter.
    command(p, {{"type", "setMidiOut"}, {"midiOut", {{"role", "bass"}, {"channel", 5}}}});
    const auto bass = play(p, head, [] { return false; }, 400);
    CHECK(bass.noteOns > 0);
    CHECK(bass.channels == std::set<int>{5});

    // Stopping the host silences everything.
    head.playing = false;
    play(p, head, [] { return false; }, 50);
    CHECK(p.auditionActiveNotes() == 0);
}

TEST_CASE("service: cancel aborts the HTTP request mid-stream") {
    const auto score = loadScore("example.json");
    auto partial = streamOf(score);
    partial.resize(3);  // header and the first part, then the model is still writing
    FakeService service(partial, true);
    ServiceUrl url(service.url());

    FlowstateProcessor p;
    p.prepareToPlay(48000.0, 480);
    FakePlayHead head;
    p.setPlayHead(&head);
    std::vector<json> events;
    p.setEventListener([&](const std::string& e) { events.push_back(json::parse(e)); });

    const auto r = command(p, {{"type", "generate"}, {"prompt", "x"}, {"roles", nullptr}, {"count", 1}, {"capture", nullptr}});
    const auto requestId = r["requestId"].get<std::string>();
    play(p, head, [&] { return !events.empty() && events.back()["type"] == "partReady"; });
    REQUIRE(events.back()["type"] == "partReady");

    command(p, {{"type", "cancel"}, {"requestId", requestId}});
    CHECK(events.back()["type"] == "generationFailed");
    CHECK(events.back()["error"]["code"] == "cancelled");
    play(p, head, [&] { return service.clientHungUp(); }, 5000);
    CHECK(service.clientHungUp());
    // The streamed idea goes; the instant sketch it had replaced stays, so there is still something to play.
    const auto nodes = json::parse(p.sessionEventJson())["session"]["nodes"];
    REQUIRE(nodes.size() == 1);
    CHECK(nodes[0]["kind"] == "sketch");
}

TEST_CASE("service: an error event, and a service that isn't there") {
    {
        FakeService service({{{"type", "error"}, {"error", {{"code", "unavailable"}, {"message", "BYOK is off"}}}}}, false, 503);
        ServiceUrl url(service.url());
        FlowstateProcessor p;
        FakePlayHead head;
        std::vector<json> events;
        p.setEventListener([&](const std::string& e) { events.push_back(json::parse(e)); });
        command(p, {{"type", "generate"}, {"prompt", "x"}, {"roles", nullptr}, {"count", 1}, {"capture", nullptr}});
        play(p, head, [&] { return events.size() > 1; });
        REQUIRE(events.size() == 2);
        CHECK(events[1]["type"] == "generationFailed");
        CHECK(events[1]["error"]["message"] == "BYOK is off");
    }
    {
        juce::StreamingSocket closed;
        REQUIRE(closed.createListener(0, "127.0.0.1"));
        const auto port = closed.getBoundPort();
        closed.close();
        ServiceUrl url("http://127.0.0.1:" + std::to_string(port));
        FlowstateProcessor p;
        FakePlayHead head;
        std::vector<json> events;
        p.setEventListener([&](const std::string& e) { events.push_back(json::parse(e)); });
        command(p, {{"type", "generate"}, {"prompt", "x"}, {"roles", nullptr}, {"count", 1}, {"capture", nullptr}});
        play(p, head, [&] { return events.size() > 1; });
        REQUIRE(events.size() == 2);
        CHECK(events[1]["error"]["code"] == "unavailable");
    }
}

// The OS keychain, in memory.
struct MemoryKeyStore final : KeyStore {
    std::map<std::string, std::string> keys;
    std::optional<std::string> write(const std::string& provider, const std::string& key) override {
        keys[provider] = key;
        return std::nullopt;
    }
    std::optional<std::string> remove(const std::string& provider) override {
        keys.erase(provider);
        return std::nullopt;
    }
    std::optional<std::string> read(const std::string& provider) override {
        const auto it = keys.find(provider);
        return it == keys.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    bool contains(const std::string& provider) override { return keys.count(provider) > 0; }
};

TEST_CASE("keys: byok mirrors the service; the key goes in the request header and never in saved state") {
    const auto score = loadScore("example.json");
    FakeService service(streamOf(score));
    ServiceUrl url(service.url());
    FlowstateProcessor p;
    auto store = std::make_unique<MemoryKeyStore>();
    auto* keys = store.get();
    p.useKeyStore(std::move(store));
    FakePlayHead head;
    std::vector<json> events;
    p.setEventListener([&](const std::string& e) { events.push_back(json::parse(e)); });

    // Opening the editor asks the service for its flags; the session follows them.
    CHECK(command(p, {{"type", "hello"}, {"protocol", "flowstate.bridge.v0"}})["session"]["settings"]["byokEnabled"] == false);
    play(p, head, [&] { return json::parse(p.sessionEventJson())["session"]["settings"]["byokEnabled"] == true; });
    CHECK(service.healthChecks == 1);
    REQUIRE(json::parse(p.sessionEventJson())["session"]["settings"]["byokEnabled"] == true);

    const std::string key = "sk-proc-SECRET-456";
    command(p, {{"type", "setProvider"}, {"provider", {{"provider", "openai"}, {"model", "gpt-5.5"}}}});
    const auto r = command(p, {{"type", "setApiKey"}, {"provider", "openai"}, {"key", key}});
    REQUIRE(r["ok"] == true);
    CHECK(r["session"]["settings"]["hasKey"] == true);
    CHECK(r.dump().find(key) == std::string::npos);
    CHECK(keys->keys["openai"] == key);

    command(p, {{"type", "generate"}, {"prompt", "x"}, {"roles", nullptr}, {"count", 1}, {"capture", nullptr}});
    play(p, head, [&] { return !events.empty() && events.back()["type"] == "generationDone"; });
    const auto request = service.request();
    CHECK(request.find("x-flowstate-provider-key: " + key) != std::string::npos);
    // In the header only: the body is the PlanRequest, which has no key.
    CHECK(request.substr(request.find("\r\n\r\n")).find(key) == std::string::npos);

    // Saved plugin state (what the DAW writes into the project) never holds the key.
    juce::MemoryBlock state;
    p.getStateInformation(state);
    CHECK(state.toString().toStdString().find(key) == std::string::npos);
    CHECK(std::string(static_cast<const char*>(state.getData()), state.getSize()).find(key) == std::string::npos);
    CHECK(p.sessionEventJson().find(key) == std::string::npos);
}

TEST_CASE("keys: a key never goes to a plain-http service that isn't on this machine") {
    ServiceUrl url("http://flowstate.invalid:8787");
    FlowstateProcessor p;
    auto store = std::make_unique<MemoryKeyStore>();
    store->keys["openai"] = "sk-x";
    p.useKeyStore(std::move(store));
    p.getController().serviceFeatures({true});
    command(p, {{"type", "setProvider"}, {"provider", {{"provider", "openai"}, {"model", "gpt-5.5"}}}});
    const auto r = command(p, {{"type", "generate"}, {"prompt", "x"}, {"roles", nullptr}, {"count", 1}, {"capture", nullptr}});
    CHECK(r["error"]["code"] == "unavailable");
    CHECK(r["error"]["message"].get<std::string>().find("https") != std::string::npos);
}

// The real OS keychain (macOS Keychain, Windows Credential Manager), under a test service name so
// it never touches the user's keys. Linux builds have none.
TEST_CASE("keys: the OS keychain stores, reads and removes a key") {
    auto store = makeOsKeyStore("Flowstate Test");
    if (store == nullptr) return;
    const std::string provider = "flowstate-test";
    REQUIRE_FALSE(store->remove(provider).has_value());
    CHECK_FALSE(store->contains(provider));
    CHECK_FALSE(store->read(provider).has_value());
    REQUIRE_FALSE(store->write(provider, "sk-first").has_value());
    CHECK(store->contains(provider));
    CHECK(store->read(provider) == std::optional<std::string>("sk-first"));
    REQUIRE_FALSE(store->write(provider, "sk-second").has_value());  // replaces
    CHECK(store->read(provider) == std::optional<std::string>("sk-second"));
    CHECK_FALSE(store->remove(provider).has_value());
    CHECK_FALSE(store->contains(provider));
    CHECK_FALSE(store->remove(provider).has_value());  // removing nothing is fine
}

// Opt-in: against a real service (`npm run serve`), set FLOWSTATE_LIVE_SERVICE_URL. Plans one idea,
// plays it, then cancels a second plan mid-stream. Skipped otherwise (CI has no model).
TEST_CASE("live service: plan, play and cancel against `npm run serve`") {
    const auto live = juce::SystemStats::getEnvironmentVariable("FLOWSTATE_LIVE_SERVICE_URL", {});
    if (live.isEmpty()) return;
    ServiceUrl url(live.toStdString());

    FlowstateProcessor p;
    p.prepareToPlay(48000.0, 480);
    FakePlayHead head;
    head.looping = false;
    head.ppq = 0.0;
    p.setPlayHead(&head);
    std::vector<json> events;
    std::vector<juce::uint32> at;
    p.setEventListener([&](const std::string& e) {
        events.push_back(json::parse(e));
        at.push_back(juce::Time::getMillisecondCounter());
    });
    const auto ended = [&] {
        return !events.empty() && (events.back()["type"] == "generationDone" || events.back()["type"] == "generationFailed");
    };

    const auto start = juce::Time::getMillisecondCounter();
    command(p, {{"type", "generate"}, {"prompt", "lofi hip hop, rainy night, mellow keys"}, {"roles", nullptr}, {"count", 1}, {"capture", nullptr}});
    const auto played = play(p, head, ended, 180000);
    REQUIRE(ended());
    for (size_t i = 0; i < events.size(); ++i)
        MESSAGE(events[i]["type"].get<std::string>() << " at " << (at[i] - start) << " ms"
                                                     << (events[i].contains("error") ? " " + events[i]["error"].dump() : ""));
    CHECK(events.back()["type"] == "generationDone");
    CHECK(played.noteOns > 0);

    events.clear();
    const auto r = command(p, {{"type", "generate"}, {"prompt", "drill beat"}, {"roles", nullptr}, {"count", 1}, {"capture", nullptr}});
    play(p, head, [&] { return !events.empty() && events.back()["type"] == "partReady"; }, 180000);
    command(p, {{"type", "cancel"}, {"requestId", r["requestId"]}});
    CHECK(events.back()["type"] == "generationFailed");
    CHECK(events.back()["error"]["code"] == "cancelled");
    play(p, head, [] { return false; }, 1000);  // the service logs the cancel
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
