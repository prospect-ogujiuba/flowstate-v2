// Session, controller, capture and state tests. JUCE-free; runs anywhere core builds.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "flowstate/analyze.h"
#include "session/Capture.h"
#include "session/Controller.h"
#include "session/PluginState.h"
#include "session/Session.h"

#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

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

// The OS keychain, in memory.
struct FakeKeyStore final : KeyStore {
    std::map<std::string, std::string> keys;
    int reads = 0;
    std::optional<std::string> write(const std::string& provider, const std::string& key) override {
        keys[provider] = key;
        return std::nullopt;
    }
    std::optional<std::string> remove(const std::string& provider) override {
        keys.erase(provider);
        return std::nullopt;
    }
    std::optional<std::string> read(const std::string& provider) override {
        ++reads;
        const auto it = keys.find(provider);
        return it == keys.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    bool contains(const std::string& provider) override { return keys.count(provider) > 0; }
};

struct FakePlatform final : Platform {
    std::int64_t now = 1790000000000;
    HostSnapshot hostState;
    int capture = 0;
    int drags = 0;
    std::string lastTitle;
    std::optional<std::vector<std::string>> lastParts;
    bool lastSplit = false;
    std::optional<fb::FocusReason> lastFocus;

    std::int64_t nowMs() override { return now++; }
    HostSnapshot host() override { return hostState; }
    int captureBars() override { return capture; }
    CaptureWindow window;  // what `generate.capture` reads
    CaptureWindow captured(int) override { return window; }
    std::optional<fb::ErrorInfo> startDrag(const fb::Clip& clip, const MidiMeta& meta,
                                           const std::optional<std::vector<std::string>>& partIds,
                                           bool splitDrums) override {
        CHECK_FALSE(clip.parts.empty());
        ++drags;
        lastTitle = meta.title;
        CHECK(meta.tempo > 0.0);
        lastParts = partIds;
        lastSplit = splitDrums;
        lastCredit = meta.credit;
        lastClip = clip;
        return std::nullopt;
    }
    std::optional<fb::ErrorInfo> exportMidi(const fb::Clip&, const MidiMeta&,
                                            const std::optional<std::vector<std::string>>&, bool) override {
        return fb::ErrorInfo{fb::ErrorCode::Cancelled, "chooser dismissed"};
    }
    void releaseFocus(fb::FocusReason reason) override { lastFocus = reason; }

    // The agent service: off unless a test turns it on; then requests are recorded, not sent.
    bool withService = false;
    std::vector<std::pair<std::string, fb::PlanRequest>> plans;
    std::vector<std::string> cancelled;
    std::vector<std::optional<std::string>> keysSent;  // each request's x-flowstate-provider-key
    std::optional<fb::ErrorInfo> startPlan(const std::string& streamId, const fb::PlanRequest& request,
                                           const std::optional<std::string>& providerKey) override {
        if (!withService) return Platform::startPlan(streamId, request, providerKey);
        plans.emplace_back(streamId, request);
        keysSent.push_back(providerKey);
        return std::nullopt;
    }
    std::vector<std::pair<std::string, fb::EditRequest>> edits;
    std::optional<fb::ErrorInfo> startEdit(const std::string& streamId, const fb::EditRequest& request,
                                           const std::optional<std::string>& providerKey) override {
        if (!withService) return Platform::startEdit(streamId, request, providerKey);
        edits.emplace_back(streamId, request);
        keysSent.push_back(providerKey);
        return std::nullopt;
    }
    void cancelStream(const std::string& streamId) override { cancelled.push_back(streamId); }
    int healthChecks = 0;
    void checkService() override { ++healthChecks; }
    KeyStore* store = nullptr;  // no keychain unless a test gives one
    KeyStore* keyStore() override { return store; }

    // The repo's built library (library/catalog), as the plugin bundles it.
    bool withLibrary = true;
    std::string lastCredit;
    std::optional<fb::Clip> lastClip;
    std::optional<std::vector<std::uint8_t>> libraryResource(const std::string& name) override {
        if (!withLibrary) return std::nullopt;
        std::ifstream in(std::string(FLOWSTATE_LIBRARY_DIR) + "/" + name, std::ios::binary);
        if (!in.good()) return std::nullopt;
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
};

json reply(Controller& c, const json& command) { return json::parse(c.handleJson(command.dump())); }

}  // namespace

TEST_CASE("lineage: add, select, undo and redo walk the tree") {
    Session s("inst", "test");
    const auto score = loadScore("example.json");

    const auto a = s.addNode(score, fb::NodeKind::Initial, "first", std::nullopt, 1, 10);
    const auto b = s.addNode(score, fb::NodeKind::Vary, std::nullopt, std::vector<std::string>{"keys"}, 2, 11);
    CHECK(s.current()->id == b);
    CHECK(s.current()->parentId == a);
    REQUIRE(s.clip().has_value());

    CHECK(s.undo());
    CHECK(s.current()->id == a);
    CHECK(s.canRedo());
    CHECK_FALSE(s.canUndo());  // a is a root
    CHECK_FALSE(s.undo());

    CHECK(s.redo());
    CHECK(s.current()->id == b);
    CHECK_FALSE(s.canRedo());

    // Branching: select the root, add a node; redo is cleared and the new node hangs off a.
    CHECK(s.select(a));
    const auto c = s.addNode(score, fb::NodeKind::Edit, "darker", std::nullopt, 3, 12);
    CHECK(s.node(c)->parentId == a);
    CHECK_FALSE(s.canRedo());
    CHECK_FALSE(s.select("nope"));
}

TEST_CASE("realization is deterministic per seed and differs across seeds") {
    const auto score = loadScore("example.json");
    const auto one = realizeToClip(score, 7);
    const auto again = realizeToClip(score, 7);
    const auto other = realizeToClip(score, 8);
    json a, b, c;
    fb::to_json(a, one);
    fb::to_json(b, again);
    fb::to_json(c, other);
    CHECK(a == b);
    CHECK(a != c);
    CHECK(one.ppq == 960);

    bool sawDrums = false;
    for (const auto& p : one.parts)
        if (p.role == fb::Role::Drums) {
            sawDrums = true;
            CHECK(p.channel == 10);
            REQUIRE(p.voices.has_value());
            CHECK_FALSE(p.voices->empty());
        } else {
            CHECK_FALSE(p.voices.has_value());
        }
    CHECK(sawDrums);
}

TEST_CASE("save and restore round-trip the whole session") {
    Session s("inst-1", "test");
    const auto score = loadScore("example.json");
    const auto a = s.addNode(score, fb::NodeKind::Initial, "first", std::nullopt, 1, 10);
    const auto b = s.addNode(score, fb::NodeKind::Vary, std::nullopt, std::nullopt, 2, 11);
    REQUIRE(s.undo());
    const auto partId = s.clip()->parts.front().partId;
    REQUIRE(s.setPartState({partId, true, false, true, 0.25}));
    fb::ContextOverride o;
    o.tempo = 101.0;
    o.tonic = fb::Tonic::FSharp;
    s.setOverride(o);
    s.setMidiOut({fb::Role::Bass, 3});
    s.setAudition({b, fb::BarRange{1, 2}, true});
    s.setPreviewSynth(false);
    s.addThreadItem({"t1", fb::ThreadRole::User, "first", a, 9});
    REQUIRE(s.rate(a, fb::Rating::Up));

    const auto saved = s.save();
    PluginState state{saved, 1000, 700};
    std::string error;
    const auto decoded = decodeState(encodeState(state), error);
    REQUIRE_MESSAGE(decoded.has_value(), error);

    Session restored("other", "test");
    std::vector<std::string> warnings;
    restored.restore(decoded->session, warnings);
    CHECK(warnings.empty());

    json before, after;
    fb::to_json(before, saved);
    fb::to_json(after, restored.save());
    CHECK(before == after);
    CHECK(restored.instanceId() == "inst-1");
    CHECK(restored.current()->id == a);
    CHECK(restored.canRedo());
    CHECK(decoded->editorWidth == 1000);

    // The view is identical too (clip included), since realization is deterministic.
    json v1, v2;
    fb::to_json(v1, s.view({}, 0));
    fb::to_json(v2, restored.view({}, 0));
    CHECK(v1 == v2);

    // New ids never collide with restored ones.
    const auto c = restored.addNode(score, fb::NodeKind::Edit, std::nullopt, std::nullopt, 4, 12);
    CHECK(c != a);
    CHECK(c != b);
}

TEST_CASE("restore drops dangling references with warnings instead of failing") {
    Session s("inst", "test");
    const auto a = s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 10);
    auto saved = s.save();
    saved.currentNodeId = "ghost";
    saved.redo = {"ghost2", a};
    saved.nodes[0].parentId = "ghost3";

    Session restored("x", "test");
    std::vector<std::string> warnings;
    restored.restore(saved, warnings);
    CHECK(warnings.size() == 3);
    CHECK(restored.current() == nullptr);
    CHECK(restored.canRedo());
    CHECK_FALSE(restored.node(a)->parentId.has_value());
}

TEST_CASE("state decoding rejects foreign, newer and malformed blobs") {
    std::string error;
    CHECK_FALSE(decodeState("", error));
    CHECK_FALSE(decodeState("<xml/>", error));
    CHECK_FALSE(decodeState(R"({"format":"someone.else"})", error));
    CHECK_FALSE(decodeState(R"({"format":"flowstate.plugin.state","version":99,"session":{}})", error));
    CHECK(error.find("newer") != std::string::npos);
    CHECK_FALSE(decodeState(R"({"format":"flowstate.plugin.state","version":1,"session":{"protocol":"flowstate.bridge.v0"}})", error));
    CHECK(error.find("session") == 0);

    // A bad editor size never fails the restore.
    Session s("inst", "test");
    auto blob = json::parse(encodeState({s.save(), 900, 600}));
    blob["editor"]["width"] = "wide";
    blob["editor"]["height"] = 5;
    const auto decoded = decodeState(blob.dump(), error);
    REQUIRE(decoded);
    CHECK(decoded->editorWidth == 960);
    CHECK(decoded->editorHeight == 480);
}

TEST_CASE("effective context: override, then host, then score, then defaults") {
    Session s("inst", "test");
    HostSnapshot host;

    auto ctx = s.effectiveContext(host);
    CHECK((ctx.keyFrom == fb::KeySource::Default));
    CHECK((ctx.timeFrom == fb::TimeSource::Default));
    CHECK(ctx.tempo == 120.0);
    CHECK(ctx.bars == 4);

    auto score = loadScore("example.json");
    s.addNode(score, fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 10);
    ctx = s.effectiveContext(host);
    CHECK((ctx.keyFrom == fb::KeySource::Score));
    CHECK((ctx.timeFrom == fb::TimeSource::Score));
    CHECK(ctx.tempo == score["context"]["tempo"].get<double>());
    CHECK(ctx.bars == score["context"]["bars"].get<int>());

    host.hasHost = true;
    host.bpm = 87.0;
    host.meterNumerator = 6;
    host.meterDenominator = 8;
    ctx = s.effectiveContext(host);
    CHECK((ctx.timeFrom == fb::TimeSource::Host));
    CHECK(ctx.tempo == 87.0);
    CHECK(ctx.meterNumerator == 6);

    fb::ContextOverride o;
    o.tempo = 140.0;
    o.tonic = fb::Tonic::Eb;
    o.bars = 16;
    s.setOverride(o);
    ctx = s.effectiveContext(host);
    CHECK((ctx.timeFrom == fb::TimeSource::Override));
    CHECK(ctx.tempo == 140.0);
    CHECK(ctx.meterNumerator == 6);  // meter still follows the host
    CHECK((ctx.keyFrom == fb::KeySource::Override));
    CHECK((ctx.tonic == fb::Tonic::Eb));
    CHECK(ctx.bars == 16);
}

TEST_CASE("host snapshot converts to bars and beats") {
    HostSnapshot h;
    h.ppq = 17.5;  // 4/4: bar 5, beat 2.5
    auto t = h.toTransport();
    CHECK(t.bar == 5);
    CHECK(t.beat == doctest::Approx(2.5));
    CHECK_FALSE(t.loop.has_value());

    h.meterNumerator = 6;
    h.meterDenominator = 8;  // 3 quarter notes per bar
    h.ppq = 4.5;             // bar 2, halfway = eighth 4 of 6
    t = h.toTransport();
    CHECK(t.bar == 2);
    CHECK(t.beat == doctest::Approx(4.0));

    h.looping = true;
    h.loopStartPpq = 0;
    h.loopEndPpq = 12;
    CHECK(h.toTransport().loop.has_value());
}

TEST_CASE("controller: hello, local commands and model commands") {
    Session s("inst", "test");
    FakePlatform platform;
    Controller c(s, platform);
    int changes = 0;
    c.onChanged = [&] { ++changes; };

    auto r = reply(c, {{"type", "hello"}, {"protocol", "flowstate.bridge.v0"}});
    CHECK(r["ok"] == true);
    CHECK(r["session"]["instanceId"] == "inst");
    CHECK(changes == 0);

    r = reply(c, {{"type", "hello"}, {"protocol", "flowstate.bridge.v9"}});
    CHECK(r["ok"] == false);
    CHECK(r["error"]["code"] == "bad_request");
    CHECK(r["error"]["message"].get<std::string>().find("protocol") != std::string::npos);

    CHECK(json::parse(c.handleJson("{not json"))["error"]["code"] == "bad_request");

    r = reply(c, {{"type", "generate"}, {"prompt", "x"}, {"roles", nullptr}, {"count", 1}, {"capture", nullptr}});
    CHECK(r["error"]["code"] == "unavailable");

    r = reply(c, {{"type", "startDrag"}, {"nodeId", nullptr}, {"partIds", nullptr}, {"splitDrums", false}});
    CHECK(r["ok"] == false);  // nothing to drag yet

    const auto a = s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 10);
    const auto partId = s.clip()->parts.front().partId;

    r = reply(c, {{"type", "setPartState"}, {"state", {{"partId", partId}, {"muted", true}, {"solo", false}, {"locked", true}, {"density", 0.5}}}});
    CHECK(r["ok"] == true);
    CHECK(r["session"]["parts"][0]["muted"] == true);
    CHECK(changes == 1);

    r = reply(c, {{"type", "setPartState"}, {"state", {{"partId", "ghost"}, {"muted", true}, {"solo", false}, {"locked", true}, {"density", 0.5}}}});
    CHECK(r["error"]["code"] == "unknown_part");

    r = reply(c, {{"type", "removePart"}, {"partId", partId}});
    CHECK(r["ok"] == true);
    CHECK(r["session"]["nodes"].size() == 2);
    CHECK(r["session"]["nodes"][1]["kind"] == "tweak");
    CHECK(r["session"]["clip"]["parts"].size() == s.node(a)->score["parts"].size() - 1);

    r = reply(c, {{"type", "undo"}});
    CHECK(r["session"]["currentNodeId"] == a);

    r = reply(c, {{"type", "startDrag"}, {"nodeId", a}, {"partIds", {partId}}, {"splitDrums", true}});
    CHECK(r["ok"] == true);
    CHECK(platform.drags == 1);
    CHECK(platform.lastParts == std::vector<std::string>{partId});
    CHECK(platform.lastSplit);
    CHECK_FALSE(platform.lastTitle.empty());

    r = reply(c, {{"type", "exportMidi"}, {"nodeId", nullptr}, {"partIds", nullptr}, {"splitDrums", false}});
    CHECK(r["error"]["code"] == "cancelled");

    r = reply(c, {{"type", "setAudition"}, {"audition", {{"nodeId", "ghost"}, {"loop", nullptr}, {"freeRun", false}}}});
    CHECK(r["error"]["code"] == "unknown_node");

    r = reply(c, {{"type", "releaseFocus"}, {"reason", "space"}});
    CHECK(r["ok"] == true);
    CHECK((platform.lastFocus == fb::FocusReason::Space));
}

TEST_CASE("controller: what the build can't do is in the session, with the reason it answers") {
    Session s("inst", "test");
    FakePlatform platform;
    Controller c(s, platform);
    s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 10);
    const auto partId = s.clip()->parts.front().partId;

    const auto session = reply(c, {{"type", "hello"}, {"protocol", "flowstate.bridge.v0"}})["session"];
    std::map<std::string, std::string> gaps;
    for (const auto& g : session["unavailable"]) gaps[g["feature"].get<std::string>()] = g["reason"].get<std::string>();

    // Each command answered `unavailable` names its feature in the session, with the same reason.
    const std::vector<std::pair<std::string, json>> commands{
        {"editNotes", {{"type", "editNotes"}, {"partId", partId}, {"remove", json::array()}, {"add", json::array()}}},
        {"apiKey", {{"type", "setApiKey"}, {"provider", "openai"}, {"key", "sk-test"}}},
    };
    for (const auto& [feature, command] : commands) {
        INFO(feature);
        const auto r = reply(c, command);
        CHECK(r["error"]["code"] == "unavailable");
        REQUIRE(gaps.count(feature) == 1);
        CHECK(r["error"]["message"] == gaps[feature]);
    }
    // Lock works since the planner keeps locked parts; tweak and the density knob since core has the transforms (P1-21).
    for (const char* local : {"lock", "tweak", "density", "reroll"}) CHECK(gaps.count(local) == 0);
    // Edit, vary and add part go to the agent service (P1-19), and so does capture (P1-20).
    for (const char* served : {"edit", "vary", "addPart", "capture"}) CHECK(gaps.count(served) == 0);
}

TEST_CASE("an API key sent over the bridge is never persisted or echoed") {
    Session s("inst", "test");
    FakePlatform platform;
    FakeKeyStore store;
    platform.store = &store;
    Controller c(s, platform);
    c.serviceFeatures({true});
    const std::string key = "sk-test-SECRET-123";
    const auto out = c.handleJson(json{{"type", "setApiKey"}, {"provider", "anthropic"}, {"key", key}}.dump());
    CHECK(json::parse(out)["ok"] == true);
    CHECK(store.keys["anthropic"] == key);
    CHECK(out.find(key) == std::string::npos);
    CHECK(encodeState({s.save(), 960, 600}).find(key) == std::string::npos);
    CHECK(json::parse(c.handleJson(json{{"type", "hello"}, {"protocol", "flowstate.bridge.v0"}}.dump())).dump().find(key) ==
          std::string::npos);

    // Also not echoed from a malformed command, or from one the keychain doesn't take.
    const auto bad = c.handleJson(json{{"type", "setApiKey"}, {"provider", 5}, {"key", key}}.dump());
    CHECK(bad.find(key) == std::string::npos);
    const std::string spaced = "sk-test SECRET\r\nX-Evil: 1";
    const auto refused = json::parse(c.handleJson(json{{"type", "setApiKey"}, {"provider", "anthropic"}, {"key", spaced}}.dump()));
    CHECK(refused["error"]["code"] == "bad_request");
    CHECK(refused.dump().find("SECRET") == std::string::npos);
    CHECK(store.keys["anthropic"] == key);
}

TEST_CASE("keys: the keychain holds them behind the byok flag; requests carry the provider's key") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withService = true;
    FakeKeyStore store;
    platform.store = &store;
    Controller c(s, platform);
    int changes = 0;
    c.onChanged = [&] { ++changes; };
    const auto setKey = [&](const std::string& provider, const json& key) {
        return reply(c, {{"type", "setApiKey"}, {"provider", provider}, {"key", key}});
    };
    const auto generate = [&] {
        const auto r = reply(c, {{"type", "generate"}, {"prompt", "x"}, {"roles", nullptr}, {"count", 2}, {"capture", nullptr}});
        REQUIRE(r["ok"] == true);
        reply(c, {{"type", "cancel"}, {"requestId", r["requestId"]}});
    };

    // With a keychain, `apiKey` is not a gap. Until the service says byok is on, the key field is
    // hidden and a key can't be stored.
    auto session = reply(c, {{"type", "hello"}, {"protocol", "flowstate.bridge.v0"}})["session"];
    CHECK(platform.healthChecks == 1);
    for (const auto& g : session["unavailable"]) CHECK(g["feature"] != "apiKey");
    CHECK(session["settings"]["byokEnabled"] == false);
    auto r = setKey("openai", "sk-one");
    CHECK(r["error"]["code"] == "unavailable");
    CHECK(store.keys.empty());

    c.serviceFeatures({true});
    CHECK(changes == 1);
    c.serviceFeatures({true});  // unchanged: no session push
    CHECK(changes == 1);
    CHECK(c.view().settings.byokEnabled);

    // The managed default takes no key; hasKey is about the session's provider.
    CHECK(setKey("openai", "sk-one")["ok"] == true);
    CHECK_FALSE(c.view().settings.hasKey);
    generate();
    REQUIRE(platform.keysSent.size() == 2);
    CHECK_FALSE(platform.keysSent[0].has_value());

    reply(c, {{"type", "setProvider"}, {"provider", {{"provider", "openai"}, {"model", "gpt-5.5"}}}});
    CHECK(c.view().settings.hasKey);
    platform.keysSent.clear();
    generate();
    REQUIRE(platform.keysSent.size() == 2);
    CHECK(platform.keysSent[0] == std::optional<std::string>("sk-one"));
    CHECK(platform.keysSent[1] == std::optional<std::string>("sk-one"));

    // Edits carry it too.
    s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 10);
    platform.keysSent.clear();
    const auto edit = reply(c, {{"type", "edit"}, {"prompt", "darker"}, {"partIds", nullptr}});
    REQUIRE(edit["ok"] == true);
    CHECK(platform.keysSent.back() == std::optional<std::string>("sk-one"));
    reply(c, {{"type", "cancel"}, {"requestId", edit["requestId"]}});

    // Another instance may change the keychain; the next request reads it afresh.
    store.keys["openai"] = "sk-two";
    platform.keysSent.clear();
    generate();
    CHECK(platform.keysSent[0] == std::optional<std::string>("sk-two"));

    // With byok turned off, no key is sent, and the field hides again.
    c.serviceFeatures({false});
    CHECK_FALSE(c.view().settings.byokEnabled);
    platform.keysSent.clear();
    generate();
    CHECK_FALSE(platform.keysSent[0].has_value());

    // Removing works with the flag off: a user can always take their key back.
    CHECK(setKey("openai", nullptr)["ok"] == true);
    CHECK(store.keys.empty());
    CHECK_FALSE(c.view().settings.hasKey);
    CHECK(setKey("", "sk-x")["error"]["code"] == "bad_request");
    CHECK(setKey("Open AI", nullptr)["error"]["code"] == "bad_request");
}

namespace {

// A riff as played: (pitch, start beat, length in beats, velocity), on the capture clock from `clock0`,
// and on the host from `host0` (-1: the host wasn't playing).
CaptureWindow played(const std::vector<std::tuple<int, double, double, int>>& notes, double clock0, double host0, int channel = 0) {
    CaptureWindow w;
    std::vector<CapturedEvent> events;
    for (const auto& [pitch, at, beats, vel] : notes) {
        const auto on = clock0 + at, off = clock0 + at + beats;
        events.push_back({on, host0 < 0 ? -1.0 : host0 + at, static_cast<std::uint8_t>(channel), static_cast<std::uint8_t>(pitch),
                          static_cast<std::uint8_t>(vel)});
        events.push_back({off, host0 < 0 ? -1.0 : host0 + at + beats, static_cast<std::uint8_t>(channel), static_cast<std::uint8_t>(pitch), 0});
    }
    std::stable_sort(events.begin(), events.end(), [](const CapturedEvent& a, const CapturedEvent& b) { return a.clockPpq < b.clockPpq; });
    w.events = events;
    w.endClockPpq = events.empty() ? clock0 : events.back().clockPpq + 1.0;
    return w;
}

// Two bars of an A minor line, eighth notes, a little off the grid.
std::vector<std::tuple<int, double, double, int>> aMinorLine() {
    const int pitches[] = {69, 72, 76, 74, 72, 71, 69, 64, 69, 72, 76, 77, 76, 74, 72, 69};
    std::vector<std::tuple<int, double, double, int>> out;
    for (int i = 0; i < 16; ++i) out.emplace_back(pitches[i], i * 0.5 + (i % 2 ? 0.02 : 0.0), 0.4, 80 + i);
    return out;
}

}  // namespace

TEST_CASE("captured notes: host bars when the host played steadily, else the first note starts bar 1") {
    // Played from host beat 9.5 (bar 3, beat 2.5): it lines up with the host's bar 3.
    auto w = played({{60, 0.0, 1.0, 90}, {64, 1.0, 1.0, 91}}, 100.0, 9.5);
    auto d = capturedNotes(w, 4.0, 110.0, 4, 4);
    REQUIRE(d.notes.size() == 2);
    CHECK(d.notes[0].tick == 1440);  // 1.5 beats into the bar
    CHECK(d.notes[1].tick == 2400);
    CHECK(d.notes[0].dur == 960);
    CHECK(d.tempo == std::optional<double>(110.0));

    // The host looped between the notes: its positions can't be trusted, so the clock decides.
    w.events[2].hostPpq = 1.0;
    d = capturedNotes(w, 4.0, 110.0, 4, 4);
    CHECK(d.notes[0].tick == 0);
    CHECK(d.notes[1].tick == 960);

    // Host stopped: the first note starts bar 1. A held note ends at the window's end, and a
    // note-off from before the window is ignored.
    w = played({{60, 0.0, 1.0, 90}}, 50.0, -1.0);
    w.events.insert(w.events.begin(), CapturedEvent{49.0, -1.0, 0, 72, 0});
    w.events.push_back({50.5, -1.0, 0, 67, 100});
    w.endClockPpq = 54.0;
    d = capturedNotes(w, 4.0, 120.0, 4, 4);
    REQUIRE(d.notes.size() == 2);
    CHECK(d.notes[0].pitch == 60);
    CHECK(d.notes[1].pitch == 67);
    CHECK(d.notes[1].tick == 480);
    CHECK(d.notes[1].dur == 3360);  // held to the end: 3.5 beats
}

TEST_CASE("capture: what was played goes as the reference, in its own key; keep intents write only the new lane") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withService = true;
    Controller c(s, platform);
    const auto use = [&](const char* intent, json roles = nullptr, const char* prompt = "") {
        return reply(c, {{"type", "generate"}, {"prompt", prompt}, {"roles", roles}, {"count", 1},
                         {"capture", {{"bars", 2}, {"intent", intent}}}});
    };

    // Continue: a new idea from the riff, at the session's length, in the riff's key (A minor).
    platform.window = played(aMinorLine(), 10.0, -1.0);
    auto r = use("continue");
    REQUIRE(r["ok"] == true);
    REQUIRE(platform.plans.size() == 1);
    auto plan = platform.plans.back().second;
    REQUIRE(plan.reference.has_value());
    CHECK((plan.reference->intent == fb::CaptureIntent::Continue));
    CHECK((plan.context.tonic == fb::Tonic::A));
    CHECK((plan.context.mode == fb::Mode::Minor));
    CHECK(plan.context.bars == 4);
    CHECK_FALSE(plan.roles.has_value());
    CHECK_FALSE(plan.keep.has_value());
    const auto& ref = plan.reference->score;
    REQUIRE(ref["parts"].size() == 1);
    CHECK(ref["parts"][0]["role"] == "melody");
    CHECK(ref["parts"][0]["blocks"][0]["notes"].size() == 16);
    json view;
    fb::to_json(view, c.view());
    CHECK(view["thread"].back()["text"] == "Continue what I played");
    c.cancelAll();

    // Add bass: the riff's length, only the bass lane, and the sketch plays the riff at once.
    r = use("add_bass");
    REQUIRE(r["ok"] == true);
    plan = platform.plans.back().second;
    CHECK(plan.context.bars == 2);
    REQUIRE(plan.roles.has_value());
    CHECK(plan.roles->size() == 1);
    CHECK((plan.roles->front() == fb::Role::Bass));
    const auto* sketch = s.current();
    REQUIRE(sketch != nullptr);
    CHECK((sketch->kind == fb::NodeKind::Sketch));
    std::set<std::string> roles;
    for (const auto& p : sketch->score["parts"]) roles.insert(p["role"].get<std::string>());
    CHECK(roles == std::set<std::string>{"melody", "bass"});
    bool literal = false;
    for (const auto& p : sketch->score["parts"])
        if (p["role"] == "melody") literal = p["blocks"][0].contains("notes");
    CHECK(literal);
    c.cancelAll();

    // The user's key wins over what core hears.
    reply(c, {{"type", "setContextOverride"}, {"override", {{"tonic", "C"}, {"mode", "major"}, {"tempo", nullptr},
                                                           {"meterNumerator", nullptr}, {"meterDenominator", nullptr},
                                                           {"bars", nullptr}, {"swing", nullptr}}}});
    r = use("answer", nullptr, "answer it higher");
    REQUIRE(r["ok"] == true);
    plan = platform.plans.back().second;
    CHECK((plan.context.tonic == fb::Tonic::C));
    CHECK((plan.context.mode == fb::Mode::Major));
    CHECK(plan.prompt == "answer it higher");
    c.cancelAll();

    // A riff too short for core to be sure of its key: the current idea's key wins over a guess...
    reply(c, {{"type", "setContextOverride"}, {"override", {{"tonic", nullptr}, {"mode", nullptr}, {"tempo", nullptr},
                                                           {"meterNumerator", nullptr}, {"meterDenominator", nullptr},
                                                           {"bars", nullptr}, {"swing", nullptr}}}});
    platform.window = played({{69, 0.0, 1.0, 90}, {72, 1.0, 1.0, 90}, {76, 2.0, 2.0, 90}}, 40.0, -1.0);
    const auto guess = flowstate::analyzeMidiData(capturedNotes(platform.window, 4.0, 120.0, 4, 4), [] {
        flowstate::AnalyzeOptions o;
        o.literalPart = true;
        return o;
    }());
    REQUIRE(guess.ok);
    REQUIRE_FALSE(guess.key.reliable);
    s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 10);  // D dorian
    REQUIRE(use("continue")["ok"] == true);
    CHECK((platform.plans.back().second.context.tonic == fb::Tonic::D));
    CHECK((platform.plans.back().second.context.mode == fb::Mode::Dorian));
    c.cancelAll();
    // ...but the default key says nothing about what was played, so core's best guess wins over it.
    Session fresh("inst2", "test");
    Controller c2(fresh, platform);
    REQUIRE(reply(c2, {{"type", "generate"}, {"prompt", ""}, {"roles", nullptr}, {"count", 1},
                       {"capture", {{"bars", 1}, {"intent", "continue"}}}})["ok"] == true);
    const auto guessed = json::parse(guess.scoreJson)["context"];
    CHECK(fb::toString(platform.plans.back().second.context.tonic) == guessed["tonic"].get<std::string>());
    CHECK(fb::toString(platform.plans.back().second.context.mode) == guessed["mode"].get<std::string>());
    c2.cancelAll();

    // Asking for the lane the riff already is: nothing is sent.
    platform.window = played(aMinorLine(), 10.0, -1.0);
    const auto sent = platform.plans.size();
    r = use("harmonize", json::array({"melody"}));
    CHECK(r["error"]["code"] == "bad_request");
    CHECK(r["error"]["message"].get<std::string>().find("already a melody") != std::string::npos);
    CHECK(platform.plans.size() == sent);
}

TEST_CASE("capture ring: SPSC order, overflow counting") {
    CaptureRing ring;
    for (int i = 0; i < 10; ++i) CHECK(ring.push({static_cast<double>(i), -1.0, 0, static_cast<std::uint8_t>(60 + i), 100}));
    std::vector<int> pitches;
    CHECK(ring.drain([&](const CapturedEvent& e) { pitches.push_back(e.pitch); }) == 10);
    CHECK(pitches.front() == 60);
    CHECK(pitches.back() == 69);

    for (std::size_t i = 0; i < CaptureRing::kCapacity; ++i) CHECK(ring.push({}));
    CHECK_FALSE(ring.push({}));
    CHECK(ring.dropped() == 1);
    CHECK(ring.drain([](const CapturedEvent&) {}) == CaptureRing::kCapacity);
}

TEST_CASE("capture history: bars available and the 64-bar window") {
    CaptureHistory h;
    const double bar = 4.0;
    CHECK(h.barsAvailable(100.0, bar) == 0);

    h.add({10.0, -1.0, 0, 60, 100});
    h.add({11.0, -1.0, 0, 60, 0});
    CHECK(h.barsAvailable(10.5, bar) == 1);
    // The pause after playing isn't part of what was played.
    CHECK(h.barsAvailable(10.0 + 3 * bar + 0.1, bar) == 1);
    CHECK(h.playedUntil(10.0 + 3 * bar + 0.1) == 11.0);
    // A note still held runs to now.
    h.add({12.0, -1.0, 0, 64, 100});
    CHECK(h.barsAvailable(10.0 + 3 * bar + 0.1, bar) == 4);
    h.add({13.0, -1.0, 0, 64, 0});

    h.trim(10.0 + 64 * bar + 1.0, bar);  // the first note is now older than 64 bars
    CHECK(h.size() == 3);                // the note-off at 11.0 and the later note survive
    CHECK(h.barsAvailable(10.0 + 64 * bar + 1.0, bar) == 1);  // the note at 12.0

    h.add({300.0, -1.0, 0, 62, 90});
    h.add({310.0, 17.0, 0, 64, 90});
    const auto last = h.lastBars(1, 311.0, bar);
    REQUIRE(last.size() == 1);
    CHECK(last[0].pitch == 64);
    CHECK(last[0].hostPpq == 17.0);
}

TEST_CASE("catalog: library clips and AI results in one search") {
    Session s("inst", "test");
    FakePlatform platform;
    Controller c(s, platform);
    const auto ai = s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::string("late-night dorian loop"),
                              std::nullopt, 1, 10);

    const json query = {{"text", ""}, {"origins", nullptr}, {"roles", nullptr}, {"genres", nullptr},
                        {"fitContext", false}, {"limit", 100}, {"offset", 0}};
    auto r = reply(c, {{"type", "searchCatalog"}, {"query", query}});
    REQUIRE(r["ok"] == true);
    CHECK(r["session"].is_null());
    const auto total = r["catalog"]["total"].get<int>();
    CHECK(total == 30);  // 29 GodFlow clips + the AI result
    int library = 0;
    for (const auto& e : r["catalog"]["entries"]) {
        if (e["origin"] == "library") {
            ++library;
            CHECK(e["credit"]["text"] == "MIDI by GodFlow (flowknows) for Flowstate.");
        } else {
            CHECK(e["id"] == "node:" + ai);
            CHECK(e["prompt"] == "late-night dorian loop");
            CHECK(e["credit"].is_null());
        }
    }
    CHECK(library == 29);

    auto q = query;
    q["origins"] = {"ai"};
    CHECK(reply(c, {{"type", "searchCatalog"}, {"query", q}})["catalog"]["total"] == 1);
    q = query;
    q["roles"] = {"bass"};
    CHECK(reply(c, {{"type", "searchCatalog"}, {"query", q}})["catalog"]["total"] == 3);  // 2 bass clips + the example's bass
    q = query;
    q["text"] = "neo-soul";
    q["limit"] = 5;
    r = reply(c, {{"type", "searchCatalog"}, {"query", q}});
    CHECK(r["catalog"]["total"] == 17);  // 16 R&B clips + the example score, tagged neo-soul
    CHECK(r["catalog"]["entries"].size() == 5);

    // Fit to the session: C major at 120 puts a C major clip (rnb-jazz-chords-15/16) first.
    s.setOverride(fb::ContextOverride{fb::Tonic::C, fb::Mode::Major, 120.0, 4, 4, std::nullopt, std::nullopt});
    q = query;
    q["roles"] = {"chords"};
    q["fitContext"] = true;
    r = reply(c, {{"type", "searchCatalog"}, {"query", q}});
    CHECK(r["catalog"]["entries"][0]["tonic"] == "C");
}

TEST_CASE("catalog: preview, use and drag a library clip; the credit travels") {
    Session s("inst", "test");
    FakePlatform platform;
    Controller c(s, platform);
    const std::string id = "lib:godflow/rnb-jazz-chords-15";

    auto r = reply(c, {{"type", "previewEntry"}, {"entryId", id}});
    CHECK(r["session"]["preview"] == id);
    CHECK(reply(c, {{"type", "previewEntry"}, {"entryId", "lib:godflow/nope"}})["error"]["code"] == "bad_request");
    CHECK(reply(c, {{"type", "previewEntry"}, {"entryId", nullptr}})["session"]["preview"].is_null());

    r = reply(c, {{"type", "dragEntry"}, {"entryId", id}});
    CHECK(r["ok"] == true);
    CHECK(platform.lastTitle == "R&B jazz chords 15");
    CHECK(platform.lastCredit == "MIDI by GodFlow (flowknows) for Flowstate.");
    REQUIRE(platform.lastClip);
    CHECK(platform.lastClip->bars == 4);
    CHECK((platform.lastClip->parts.front().role == fb::Role::Chords));
    CHECK(platform.lastClip->parts.front().notes.size() == 19);  // the notes as written, not the IR's

    r = reply(c, {{"type", "useEntry"}, {"entryId", id}});
    REQUIRE(r["ok"] == true);
    const auto& node = r["session"]["nodes"].back();
    CHECK(node["kind"] == "library");
    CHECK(node["entryId"] == id);
    CHECK(r["session"]["clip"]["parts"].size() == 1);
    const auto libNode = node["id"].get<std::string>();

    // An edit of it keeps the credit; a fresh plan doesn't; AI search leaves library nodes out.
    s.addNode(s.current()->score, fb::NodeKind::Tweak, std::nullopt, std::nullopt, 2, 20);
    CHECK(s.current()->entryId == id);
    r = reply(c, {{"type", "startDrag"}, {"nodeId", nullptr}, {"partIds", nullptr}, {"splitDrums", false}});
    CHECK(platform.lastCredit == "MIDI by GodFlow (flowknows) for Flowstate.");
    s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::string("x"), std::nullopt, 3, 30);
    CHECK_FALSE(s.current()->entryId);
    const json aiOnly = {{"text", ""}, {"origins", {"ai"}}, {"roles", nullptr}, {"genres", nullptr},
                         {"fitContext", false}, {"limit", 10}, {"offset", 0}};
    r = reply(c, {{"type", "searchCatalog"}, {"query", aiOnly}});
    CHECK(r["catalog"]["total"] == 1);

    // Using an AI result selects its node.
    r = reply(c, {{"type", "useEntry"}, {"entryId", "node:" + libNode}});
    CHECK(r["session"]["currentNodeId"] == libNode);
    CHECK(reply(c, {{"type", "useEntry"}, {"entryId", "node:ghost"}})["error"]["code"] == "unknown_node");
}

TEST_CASE("catalog: a build without a library searches AI results only") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withLibrary = false;
    Controller c(s, platform);
    s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 10);
    const json query = {{"text", ""}, {"origins", nullptr}, {"roles", nullptr}, {"genres", nullptr},
                        {"fitContext", false}, {"limit", 10}, {"offset", 0}};
    CHECK(reply(c, {{"type", "searchCatalog"}, {"query", query}})["catalog"]["total"] == 1);
    CHECK(reply(c, {{"type", "useEntry"}, {"entryId", "lib:godflow/bass-02"}})["error"]["code"] == "bad_request");
}

TEST_CASE("state version 1 (before library nodes) still restores") {
    Session s("inst", "test");
    s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 7, 10);
    PluginState state;
    state.session = s.save();
    auto blob = json::parse(encodeState(state));
    blob["version"] = 1;
    for (auto& n : blob["session"]["nodes"]) n.erase("entryId");
    std::string error;
    const auto decoded = decodeState(blob.dump(), error);
    REQUIRE_MESSAGE(decoded, error);
    CHECK_FALSE(decoded->session.nodes.front().entryId);
}

// ---- Generate: the agent service's stream becomes nodes (P1-4 client, P1-7) -----------------------

namespace {

json generateCommand(const char* prompt, int count = 1) {
    return {{"type", "generate"}, {"prompt", prompt}, {"roles", nullptr}, {"count", count}, {"capture", nullptr}};
}

fb::ServiceEvent headerOf(json score) {
    score["parts"] = json::array();
    return fb::ScoreHeader{score};
}

struct Events {
    std::vector<json> list;
    void attach(Controller& c) {
        c.onEvent = [this](const fb::PluginEvent& e) {
            json j;
            fb::to_json(j, e);
            list.push_back(j);
        };
    }
    std::vector<std::string> types() const {
        std::vector<std::string> out;
        for (const auto& e : list) out.push_back(e["type"]);
        return out;
    }
};

}  // namespace

TEST_CASE("generate: a PlanRequest goes out, streamed parts become a node that plays, done finishes it") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withService = true;
    Controller c(s, platform);
    Events events;
    events.attach(c);
    int changes = 0;
    c.onChanged = [&] { ++changes; };
    const auto score = loadScore("example.json");

    auto r = reply(c, generateCommand("late night keys"));
    REQUIRE(r["ok"] == true);
    const auto requestId = r["requestId"].get<std::string>();
    REQUIRE(platform.plans.size() == 1);
    const auto& [streamId, plan] = platform.plans[0];
    CHECK(plan.prompt == "late night keys");
    // The effective context: C major by default, the host's tempo and meter, 4 bars.
    CHECK((plan.context.tonic == fb::Tonic::C));
    CHECK(plan.context.bars == 4);
    CHECK(plan.context.tempo == 120.0);
    CHECK_FALSE(plan.keep.has_value());
    CHECK(r["session"]["generations"].size() == 1);
    CHECK(r["session"]["generations"][0]["stage"] == "planning");
    CHECK(r["session"]["thread"][0]["role"] == "user");
    CHECK(events.types() == std::vector<std::string>{"generationStarted"});
    CHECK(c.generating());

    // A second generate while one runs is refused.
    CHECK(reply(c, generateCommand("again"))["error"]["code"] == "busy");

    c.serviceEvent(streamId, headerOf(score));
    CHECK((c.view().generations[0].stage == fb::GenerationStage::Streaming));
    // Before any part lands, the instant sketch plays: all four roles, from core's rules (P1-10).
    REQUIRE(s.nodes().size() == 1);
    CHECK((s.current()->kind == fb::NodeKind::Sketch));
    CHECK(s.current()->prompt == "late night keys");
    REQUIRE(s.clip().has_value());
    CHECK(s.clip()->parts.size() == 4);
    const auto sketchId = s.current()->id;
    const auto partIdsOf = [&] {
        std::vector<std::string> ids;
        for (const auto& p : s.clip()->parts) ids.push_back(p.partId);
        return ids;
    };

    c.serviceEvent(streamId, fb::PartStarted{"keys", fb::Role::Chords});
    c.serviceEvent(streamId, fb::PartDone{score["parts"][0]});
    // The first part takes the sketch's place; the sketch fills the roles that haven't arrived. It stays in the
    // lineage (hidden in the Studio) until the plan is done, so a failure could go back to it.
    REQUIRE(s.nodes().size() == 2);
    const auto nodeId = s.nodes()[1].id;
    CHECK(nodeId != sketchId);
    CHECK(s.hasNode(sketchId));
    CHECK(s.current()->id == nodeId);
    CHECK_FALSE(s.current()->parentId.has_value());
    REQUIRE(s.clip().has_value());
    CHECK(partIdsOf() == std::vector<std::string>{"keys", "sketch-bass", "sketch-melody", "sketch-drums"});
    CHECK(events.list.back()["type"] == "partReady");
    CHECK(events.list.back()["partId"] == "keys");

    c.serviceEvent(streamId, fb::PartDone{score["parts"][1]});
    c.serviceEvent(streamId, fb::PartDone{score["parts"][0]});  // a repaired part replaces the first
    CHECK(s.nodes().size() == 2);
    CHECK(partIdsOf() == std::vector<std::string>{"keys", "bass", "sketch-melody", "sketch-drums"});
    CHECK(c.view().generations[0].partsDone == std::vector<std::string>{"keys", "bass"});

    c.serviceEvent(streamId, fb::AssistantMessage{"A slow neo-soul loop."});
    c.serviceEvent(streamId, fb::ScoreDone{score});
    CHECK(s.node(nodeId)->score == score);  // the authoritative score, with no sketch parts
    CHECK(s.nodes().size() == 1);           // done: the sketch is gone
    CHECK_FALSE(s.hasNode(sketchId));
    CHECK(s.clip()->parts.size() == 4);
    CHECK((s.node(nodeId)->kind == fb::NodeKind::Initial));
    CHECK(s.node(nodeId)->prompt == "late night keys");
    CHECK_FALSE(c.generating());
    CHECK(c.view().generations.empty());
    REQUIRE(events.list.back()["type"] == "generationDone");
    CHECK(events.list.back()["requestId"] == requestId);
    CHECK(events.list.back()["nodeIds"] == json::array({nodeId}));
    const auto thread = s.thread();
    REQUIRE(thread.size() == 2);
    CHECK((thread[1].role == fb::ThreadRole::Assistant));
    CHECK(thread[1].text == "A slow neo-soul loop.");
    CHECK(thread[1].nodeId == nodeId);

    // The stream closing afterwards, or a stray event, changes nothing.
    const auto before = events.list.size();
    c.serviceEnded(streamId, std::nullopt);
    c.serviceEvent(streamId, fb::PartDone{score["parts"][2]});
    CHECK(events.list.size() == before);
    CHECK(changes > 0);

    // The idea re-realizes identically from the saved session.
    const auto saved = s.save();
    Session restored("inst", "test");
    std::vector<std::string> warnings;
    restored.restore(saved, warnings);
    CHECK(restored.clip()->parts[0].notes.size() == s.clip()->parts[0].notes.size());
}

TEST_CASE("generate: variations stream in parallel; the first to land plays, a failed one leaves nothing") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withService = true;
    Controller c(s, platform);
    Events events;
    events.attach(c);
    const auto score = loadScore("example.json");
    const auto parent = s.addNode(score, fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 10);

    const auto r = reply(c, generateCommand("variations", 3));
    REQUIRE(platform.plans.size() == 3);
    const auto one = platform.plans[0].first, two = platform.plans[1].first, three = platform.plans[2].first;
    CHECK(one != two);
    CHECK((platform.plans[0].second.context.tonic == fb::Tonic::D));  // from the current idea
    CHECK(platform.plans[0].second.context.swing == 0.15);

    for (const auto& id : {one, two, three}) c.serviceEvent(id, headerOf(score));
    c.serviceEvent(two, fb::PartDone{score["parts"][0]});
    const auto playing = s.current()->id;
    CHECK(playing != parent);
    CHECK(s.current()->parentId == parent);
    c.serviceEvent(one, fb::PartDone{score["parts"][1]});
    // The parent, the hidden sketch, and two variations.
    CHECK(s.nodes().size() == 4);
    CHECK(s.current()->id == playing);  // a later variation doesn't take over
    CHECK(s.node(s.nodes()[3].id)->parentId == parent);
    CHECK(s.node(s.nodes()[3].id)->seed != s.node(playing)->seed);

    c.serviceEvent(one, fb::ServiceError{{fb::ErrorCode::Provider, "upstream 502"}});
    CHECK(s.nodes().size() == 3);  // its partial idea is gone
    c.serviceEnded(three, fb::ErrorInfo{fb::ErrorCode::Network, "reset"});
    CHECK(c.generating());
    c.serviceEvent(two, fb::ScoreDone{score});

    CHECK_FALSE(c.generating());
    const auto types = events.types();
    CHECK(types[types.size() - 2] == "generationDone");
    CHECK(events.list[types.size() - 2]["nodeIds"] == json::array({playing}));
    CHECK(types.back() == "notice");
    CHECK(s.current()->id == playing);
    CHECK(s.nodes().size() == 2);  // a variation succeeded, so the sketch is gone
    CHECK(r["requestId"] == events.list[0]["requestId"]);
}

TEST_CASE("generate: cancel stops every stream and takes back what streamed") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withService = true;
    Controller c(s, platform);
    Events events;
    events.attach(c);
    const auto score = loadScore("example.json");

    const auto r = reply(c, generateCommand("x", 2));
    const auto requestId = r["requestId"].get<std::string>();
    const auto stream = platform.plans[0].first;
    c.serviceEvent(stream, headerOf(score));
    c.serviceEvent(stream, fb::PartDone{score["parts"][0]});
    CHECK(s.current() != nullptr);

    CHECK(reply(c, {{"type", "cancel"}, {"requestId", "nope"}})["error"]["code"] == "bad_request");
    const auto cancelled = reply(c, {{"type", "cancel"}, {"requestId", requestId}});
    CHECK(cancelled["ok"] == true);
    CHECK(platform.cancelled.size() == 2);
    // What streamed goes, and playback goes back to the instant sketch it had replaced.
    REQUIRE(s.nodes().size() == 1);
    CHECK((s.current()->kind == fb::NodeKind::Sketch));
    CHECK(s.clip().has_value());
    CHECK(events.list.back()["type"] == "generationFailed");
    CHECK(events.list.back()["error"]["code"] == "cancelled");
    CHECK(cancelled["session"]["generations"].empty());

    // Whatever was still in flight is ignored.
    c.serviceEvent(stream, fb::PartDone{score["parts"][1]});
    c.serviceEvent(stream, fb::ScoreDone{score});
    CHECK(s.nodes().size() == 1);

    // Cancelled before any part landed: that sketch stays too, under the idea that was current.
    const auto sketch = s.current()->id;
    const auto early = reply(c, generateCommand("z"))["requestId"].get<std::string>();
    REQUIRE(reply(c, {{"type", "cancel"}, {"requestId", early}})["ok"] == true);
    REQUIRE(s.nodes().size() == 2);
    CHECK((s.current()->kind == fb::NodeKind::Sketch));
    CHECK(s.current()->parentId == sketch);

    // A new generate can start.
    CHECK(reply(c, generateCommand("y"))["ok"] == true);
    c.cancelAll();
    CHECK_FALSE(c.generating());
}

TEST_CASE("generate: errors, early close, text-only answers, keep and capture") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withService = true;
    Controller c(s, platform);
    Events events;
    events.attach(c);
    const auto score = loadScore("example.json");

    // The service refuses before streaming.
    reply(c, generateCommand("x"));
    c.serviceEvent(platform.plans.back().first, fb::ServiceError{{fb::ErrorCode::Refused, "The model refused."}});
    CHECK(events.list.back()["type"] == "generationFailed");
    CHECK(events.list.back()["error"]["code"] == "refused");

    // The connection drops mid-plan: a network failure, and the partial idea goes.
    reply(c, generateCommand("x"));
    auto id = platform.plans.back().first;
    c.serviceEvent(id, headerOf(score));
    c.serviceEvent(id, fb::PartDone{score["parts"][0]});
    c.serviceEnded(id, std::nullopt);
    CHECK(events.list.back()["error"]["code"] == "network");
    // No AI idea is left; each failed request's instant sketch stays, and the last one plays.
    const auto onlySketches = [&] {
        return std::all_of(s.nodes().begin(), s.nodes().end(), [](const fb::LineageNode& n) { return n.kind == fb::NodeKind::Sketch; });
    };
    CHECK(onlySketches());
    CHECK(s.nodes().size() == 2);
    CHECK((s.current()->kind == fb::NodeKind::Sketch));

    // A question answered with text: done without a score, no node, an answer in the thread, and its sketch goes.
    reply(c, generateCommand("what swing suits house?"));
    id = platform.plans.back().first;
    CHECK(s.nodes().size() == 3);
    c.serviceEvent(id, fb::AssistantMessage{"Around 55 to 58 percent."});
    c.serviceEvent(id, fb::ScoreDone{std::nullopt});
    CHECK(events.list.back()["type"] == "generationDone");
    CHECK(events.list.back()["nodeIds"].empty());
    CHECK(s.nodes().size() == 2);
    CHECK(onlySketches());
    CHECK(s.thread().back().text == "Around 55 to 58 percent.");
    CHECK_FALSE(s.thread().back().nodeId.has_value());

    // Locked parts travel as `keep`, with the harmony.
    s.addNode(score, fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 10);
    reply(c, {{"type", "setPartState"}, {"state", {{"partId", "bass"}, {"muted", false}, {"solo", false}, {"locked", true}, {"density", 0.5}}}});
    reply(c, generateCommand("new melody"));
    const auto& keep = platform.plans.back().second.keep;
    REQUIRE(keep.has_value());
    CHECK((*keep)["parts"].size() == 1);
    CHECK((*keep)["parts"][0]["id"] == "bass");
    CHECK((*keep)["harmony"] == score["harmony"]);
    c.cancelAll();

    // "Use what I just played" with nothing played: nothing is sent.
    const auto sent = platform.plans.size();
    auto r = reply(c, {{"type", "generate"}, {"prompt", ""}, {"roles", nullptr}, {"count", 1},
                       {"capture", {{"bars", 4}, {"intent", "continue"}}}});
    CHECK(r["error"]["code"] == "bad_request");
    CHECK(r["error"]["message"].get<std::string>().find("Nothing was played") != std::string::npos);
    CHECK(platform.plans.size() == sent);

    // A request that can't be sent: nothing starts.
    platform.withService = false;
    const auto plans = platform.plans.size();
    r = reply(c, generateCommand("x"));
    CHECK(r["error"]["code"] == "unavailable");
    CHECK(platform.plans.size() == plans);
    CHECK_FALSE(c.generating());
}

TEST_CASE("audition source: the current node, a chosen node, or a catalog preview, with this instance's filter") {
    Session s("inst", "test");
    FakePlatform platform;
    Controller c(s, platform);
    CHECK_FALSE(c.audition().clip.has_value());

    const auto a = s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 10);
    const auto b = s.addNode(loadScore("seven_eight.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 2, 11);
    auto source = c.audition();
    REQUIRE(source.clip.has_value());
    CHECK(source.clip->parts.size() == s.clip()->parts.size());

    reply(c, {{"type", "setAudition"}, {"audition", {{"nodeId", a}, {"loop", {{"startBar", 2}, {"endBar", 3}}}, {"freeRun", true}}}});
    reply(c, {{"type", "setMidiOut"}, {"midiOut", {{"role", "bass"}, {"channel", 3}}}});
    source = c.audition();
    CHECK(source.clip->parts.size() == s.node(a)->score["parts"].size());
    CHECK(source.filter.loop->startBar == 2);
    CHECK((source.filter.role == fb::Role::Bass));
    const auto rendered = renderAudition(*source.clip, source.filter);
    CHECK(rendered.lengthPpq == 8.0);
    for (const auto& e : rendered.events) CHECK(e.channel == source.clip->parts[1].channel - 1);

    // A catalog preview plays as written.
    const auto search = reply(c, {{"type", "searchCatalog"},
                                  {"query", {{"text", ""}, {"origins", {"library"}}, {"roles", nullptr}, {"genres", nullptr},
                                             {"fitContext", false}, {"limit", 1}, {"offset", 0}}}});
    const auto entry = search["catalog"]["entries"][0]["id"].get<std::string>();
    reply(c, {{"type", "previewEntry"}, {"entryId", entry}});
    source = c.audition();
    REQUIRE(source.clip.has_value());
    CHECK_FALSE(source.filter.role.has_value());
    CHECK_FALSE(source.filter.loop.has_value());
    (void)b;
}

// ---- Lineage at scale and its bounds (P1-11) -------------------------------------------------------

namespace {

// Streams one whole plan through the controller, the way the service sends it.
std::string streamPlan(Controller& c, FakePlatform& platform, const json& score, const std::string& prompt) {
    const auto r = reply(c, generateCommand(prompt.c_str()));
    REQUIRE(r["ok"] == true);
    const auto stream = platform.plans.back().first;
    c.serviceEvent(stream, headerOf(score));
    for (const auto& p : score["parts"]) c.serviceEvent(stream, fb::PartDone{p});
    c.serviceEvent(stream, fb::ScoreDone{score});
    c.serviceEnded(stream, std::nullopt);
    REQUIRE_FALSE(c.generating());
    return r["requestId"].get<std::string>();
}

bool treeIsWhole(const Session& s) {
    for (const auto& n : s.nodes())
        if (n.parentId && !s.hasNode(*n.parentId)) return false;
    for (const auto& t : s.thread())
        if (t.nodeId && !s.hasNode(*t.nodeId)) return false;
    return true;
}

}  // namespace

TEST_CASE("lineage: 50 generations in one session; undo, redo and A/B walk them; the project reopens with all of them") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withService = true;
    Controller c(s, platform);
    const auto score = loadScore("example.json");

    std::vector<std::string> ids;
    for (int i = 0; i < 50; ++i) {
        streamPlan(c, platform, score, "idea " + std::to_string(i));
        ids.push_back(s.current()->id);
    }
    REQUIRE(s.nodes().size() == 50);
    CHECK(s.thread().size() == 100);  // a prompt and a result card each
    CHECK(std::set<std::string>(ids.begin(), ids.end()).size() == 50);
    for (std::size_t i = 1; i < ids.size(); ++i) CHECK(s.node(ids[i])->parentId == ids[i - 1]);
    CHECK((s.node(ids[0])->kind == fb::NodeKind::Initial));

    // A/B between two results, then back.
    CHECK(reply(c, {{"type", "selectNode"}, {"nodeId", ids[10]}})["session"]["currentNodeId"] == ids[10]);
    CHECK(reply(c, {{"type", "selectNode"}, {"nodeId", ids[49]}})["session"]["currentNodeId"] == ids[49]);

    // Undo walks back through all 50, redo walks forward again.
    for (int i = 0; i < 49; ++i) REQUIRE(reply(c, {{"type", "undo"}})["ok"] == true);
    CHECK(s.current()->id == ids[0]);
    CHECK_FALSE(s.canUndo());
    for (int i = 0; i < 49; ++i) REQUIRE(reply(c, {{"type", "redo"}})["ok"] == true);
    CHECK(s.current()->id == ids[49]);

    // Lock a part: the lock is in the saved state too.
    const auto partId = s.clip()->parts.front().partId;
    REQUIRE(reply(c, {{"type", "setPartState"}, {"state", {{"partId", partId}, {"muted", false}, {"solo", false}, {"locked", true}, {"density", 0.5}}}})["ok"] == true);

    // Save the project: well under 1 MB, and it reopens with every idea, the thread and the lock.
    const auto blob = encodeState({s.save(), 960, 600});
    CHECK(blob.size() < 1024 * 1024);
    std::string error;
    const auto decoded = decodeState(blob, error);
    REQUIRE_MESSAGE(decoded.has_value(), error);
    Session reopened("other", "test");
    std::vector<std::string> warnings;
    reopened.restore(decoded->session, warnings);
    CHECK(warnings.empty());
    json before, after;
    fb::to_json(before, s.view({}, 0));
    fb::to_json(after, reopened.view({}, 0));
    CHECK(before == after);
    CHECK(reopened.partStates().front().locked);

    // Generating on after reopening makes fresh ids under the restored idea.
    FakePlatform platform2;
    platform2.withService = true;
    Controller c2(reopened, platform2);
    streamPlan(c2, platform2, score, "one more");
    CHECK(reopened.nodes().size() == 51);
    CHECK(std::find(ids.begin(), ids.end(), reopened.current()->id) == ids.end());
    CHECK(reopened.current()->parentId == ids[49]);
}

TEST_CASE("lineage: bounded; the oldest go first, never the recent undo path, the audition or a pinned node") {
    Session s("inst", "test");
    const auto score = loadScore("example.json");
    std::vector<std::string> ids;
    for (int i = 0; i < 260; ++i) {
        ids.push_back(s.addNode(score, fb::NodeKind::Initial, std::nullopt, std::nullopt, i + 1, i));
        if (i == 2) s.setAudition({ids[2], std::nullopt, false});
        if (i == 3) s.pin(ids[3]);
    }
    CHECK(s.nodes().size() <= Session::kMaxNodes);
    CHECK(s.scoreBytes() <= Session::kMaxScoreBytes);
    CHECK(s.hasNode(ids[2]));   // auditioned
    CHECK(s.hasNode(ids[3]));   // pinned
    CHECK_FALSE(s.hasNode(ids[4]));  // the oldest unprotected go first
    CHECK(s.current()->id == ids.back());
    for (std::size_t back = 0; back <= Session::kUndoDepth; ++back) CHECK(s.hasNode(ids[ids.size() - 1 - back]));
    CHECK(treeIsWhole(s));

    // Undo still walks the whole way down: past the pruned nodes, to the surviving ancestors.
    std::size_t undos = 0;
    while (s.undo()) ++undos;
    CHECK(undos == s.nodes().size() - 1);

    // Saved, it stays well under 1 MB.
    CHECK(encodeState({s.save(), 960, 600}).size() < 1024 * 1024);

    // The thread keeps its newest items.
    for (int i = 0; i < 500; ++i) s.addThreadItem({"t" + std::to_string(i), fb::ThreadRole::User, "p", std::nullopt, i});
    CHECK(s.thread().size() == Session::kMaxThreadItems);
    CHECK(s.thread().back().id == "t499");
}

TEST_CASE("lineage: a streaming node and the node it grows from are never pruned") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withService = true;
    Controller c(s, platform);
    const auto score = loadScore("example.json");
    const auto parent = s.addNode(score, fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 1);

    REQUIRE(reply(c, generateCommand("slow one"))["ok"] == true);
    const auto stream = platform.plans.back().first;
    c.serviceEvent(stream, headerOf(score));
    c.serviceEvent(stream, fb::PartDone{score["parts"][0]});
    const auto streaming = s.current()->id;
    // The user moves on and makes far more ideas than the bound while it streams.
    for (int i = 0; i < 260; ++i) s.addNode(score, fb::NodeKind::Initial, std::nullopt, std::nullopt, i + 2, i + 2, std::string("nowhere"));
    CHECK(s.hasNode(parent));
    CHECK(s.hasNode(streaming));
    c.serviceEvent(stream, fb::PartDone{score["parts"][1]});
    c.serviceEvent(stream, fb::ScoreDone{score});
    CHECK(s.node(streaming)->score == score);
    CHECK(s.node(streaming)->parentId == parent);
    CHECK(treeIsWhole(s));
}

TEST_CASE("restore: an oversized or dangling saved lineage is trimmed and repaired, with warnings") {
    Session s("inst", "test");
    const auto score = loadScore("example.json");
    auto saved = s.save();
    for (int i = 1; i <= 250; ++i) {
        fb::LineageNode n;
        n.id = "n" + std::to_string(i);
        n.kind = fb::NodeKind::Initial;
        n.createdAtMs = i;
        n.seed = i;
        n.score = score;
        saved.nodes.push_back(n);
    }
    saved.currentNodeId = "n250";
    saved.redo = {"n1"};  // odd, but valid: redo is protected
    saved.thread.push_back({"t-gone", fb::ThreadRole::Assistant, "an idea", std::string("n999"), 1});
    saved.audition = {std::string("n998"), std::nullopt, false};

    Session restored("x", "test");
    std::vector<std::string> warnings;
    restored.restore(saved, warnings);
    CHECK(restored.nodes().size() <= Session::kMaxNodes);
    CHECK(restored.hasNode("n1"));
    CHECK(restored.current()->id == "n250");
    CHECK_FALSE(restored.thread().back().nodeId.has_value());
    CHECK(restored.thread().back().text == "an idea");
    CHECK_FALSE(restored.audition().nodeId.has_value());
    CHECK(treeIsWhole(restored));
    const auto has = [&](const char* what) {
        return std::any_of(warnings.begin(), warnings.end(), [&](const std::string& w) { return w.find(what) != std::string::npos; });
    };
    CHECK(has("size bound"));
    CHECK(has("missing node"));
    CHECK(has("auditioned node"));
    // New ids continue after the highest restored one, so a removed id is never reused.
    CHECK(restored.addNode(score, fb::NodeKind::Edit, std::nullopt, std::nullopt, 1, 1) == "n251");
}

TEST_CASE("generate: the instant sketch keeps locked parts and their harmony, and sketches the rest (P1-10)") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withService = true;
    Controller c(s, platform);
    const auto score = loadScore("example.json");
    const auto idea = s.addNode(score, fb::NodeKind::Initial, std::nullopt, std::nullopt, 1, 10);
    REQUIRE(reply(c, {{"type", "setPartState"}, {"state", {{"partId", "bass"}, {"muted", false}, {"solo", false}, {"locked", true}, {"density", 0.5}}}})["ok"] == true);

    REQUIRE(reply(c, generateCommand("dark trap"))["ok"] == true);
    const auto* sketch = s.current();
    REQUIRE((sketch->kind == fb::NodeKind::Sketch));
    CHECK(sketch->parentId == idea);
    CHECK(sketch->score["harmony"] == score["harmony"]);
    std::vector<std::string> ids;
    for (const auto& p : sketch->score["parts"]) ids.push_back(p["id"]);
    CHECK(ids == std::vector<std::string>{"bass", "sketch-chords", "sketch-melody", "sketch-drums"});
    CHECK(sketch->score["parts"][0] == score["parts"][1]);  // the locked bass, verbatim
    // The prompt is a style hint: "trap" picks the trap groove.
    CHECK(sketch->score["parts"][3]["blocks"][0]["groove"] == "trap");
    REQUIRE(s.clip().has_value());
    CHECK(s.clip()->parts.size() == 4);
    c.cancelAll();
}

// ---- Edit, vary and add part: EditRequests to the agent service (P1-19) -------------------------------

namespace {

std::vector<std::string> partIdsOf(const json& score) {
    std::vector<std::string> ids;
    for (const auto& p : score["parts"]) ids.push_back(p["id"]);
    return ids;
}

}  // namespace

TEST_CASE("edit: an EditRequest from the current idea; streamed parts replace by id; done is authoritative") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withService = true;
    Controller c(s, platform);
    Events events;
    events.attach(c);
    const auto score = loadScore("example.json");  // parts keys, bass, lead, drums
    const auto idea = s.addNode(score, fb::NodeKind::Initial, std::string("slow neo-soul"), std::nullopt, 77, 10);
    s.addThreadItem({"t1", fb::ThreadRole::Assistant, "A slow neo-soul loop.", idea, 11});
    REQUIRE(reply(c, {{"type", "setPartState"}, {"state", {{"partId", "bass"}, {"muted", false}, {"solo", false}, {"locked", true}, {"density", 0.5}}}})["ok"] == true);

    auto r = reply(c, {{"type", "edit"}, {"prompt", "darker, drop the lead"}, {"partIds", nullptr}});
    REQUIRE(r["ok"] == true);
    REQUIRE(platform.edits.size() == 1);
    const auto& [streamId, req] = platform.edits[0];
    CHECK((req.kind == fb::EditKind::Edit));
    CHECK(req.prompt == "darker, drop the lead");
    CHECK(req.score == score);
    CHECK(req.partIds == std::vector<std::string>{"keys", "lead", "drums"});  // the locked bass is left out
    CHECK_FALSE(req.role.has_value());
    REQUIRE(req.history.size() == 1);
    CHECK((req.history[0].kind == fb::NodeKind::Initial));
    CHECK(req.history[0].prompt == "slow neo-soul");
    CHECK(req.history[0].note == "A slow neo-soul loop.");
    CHECK(req.history[0].changed.empty());
    CHECK(r["session"]["generations"][0]["kind"] == "edit");
    CHECK(events.list.back()["type"] == "generationStarted");
    CHECK(s.current()->id == idea);  // no sketch for edits

    // The edited head, then only the changed part.
    auto head = score;
    head["title"] = "Darker";
    head["parts"] = json::array();
    c.serviceEvent(streamId, fb::ScoreHeader{head});
    auto darkKeys = score["parts"][0];
    darkKeys["name"] = "Dark keys";
    c.serviceEvent(streamId, fb::PartDone{darkKeys});
    REQUIRE(s.nodes().size() == 2);
    const auto edited = s.current()->id;
    CHECK(edited != idea);
    CHECK((s.current()->kind == fb::NodeKind::Edit));
    CHECK(s.current()->parentId == idea);
    CHECK(s.current()->seed == 77);  // unchanged parts realize as before
    CHECK(partIdsOf(s.current()->score) == std::vector<std::string>{"keys", "bass", "lead", "drums"});
    CHECK(s.current()->score["parts"][0]["name"] == "Dark keys");
    CHECK(s.current()->score["parts"][1] == score["parts"][1]);

    // done: the whole score; the lead is gone, which shows only here.
    auto final = score;
    final["title"] = "Darker";
    final["parts"] = json::array({darkKeys, score["parts"][1], score["parts"][3]});
    c.serviceEvent(streamId, fb::AssistantMessage{"Darker keys; dropped the lead."});
    c.serviceEvent(streamId, fb::ScoreDone{final});
    CHECK_FALSE(c.generating());
    CHECK(s.node(edited)->score == final);
    CHECK(s.node(edited)->partIds == std::vector<std::string>{"keys", "lead"});  // changed: streamed, and removed
    CHECK(events.list.back()["type"] == "generationDone");
    CHECK(s.thread().back().text == "Darker keys; dropped the lead.");
    CHECK(s.thread().back().nodeId == edited);

    // The next edit carries the path, with what each step changed.
    REQUIRE(reply(c, {{"type", "edit"}, {"prompt", "less than that"}, {"partIds", json::array({"keys"})}})["ok"] == true);
    const auto& next = platform.edits.back().second;
    CHECK(next.partIds == std::vector<std::string>{"keys"});
    REQUIRE(next.history.size() == 2);
    CHECK((next.history[1].kind == fb::NodeKind::Edit));
    CHECK(next.history[1].prompt == "darker, drop the lead");
    CHECK(next.history[1].note == "Darker keys; dropped the lead.");
    CHECK(next.history[1].changed == std::vector<std::string>{"keys", "lead"});
    c.cancelAll();
}

TEST_CASE("edit: vary and add part; refusals before anything is sent; a question makes no node") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withService = true;
    Controller c(s, platform);
    const auto score = loadScore("example.json");

    // No idea yet.
    CHECK(reply(c, {{"type", "vary"}, {"partId", "lead"}})["error"]["code"] == "bad_request");
    const auto idea = s.addNode(score, fb::NodeKind::Initial, std::nullopt, std::nullopt, 5, 10);

    // Vary: exactly one part, never a locked or unknown one.
    CHECK(reply(c, {{"type", "vary"}, {"partId", "ghost"}})["error"]["code"] == "unknown_part");
    REQUIRE(reply(c, {{"type", "setPartState"}, {"state", {{"partId", "drums"}, {"muted", false}, {"solo", false}, {"locked", true}, {"density", 0.5}}}})["ok"] == true);
    auto r = reply(c, {{"type", "vary"}, {"partId", "drums"}});
    CHECK(r["error"]["code"] == "bad_request");
    CHECK(r["error"]["message"].get<std::string>().find("locked") != std::string::npos);
    CHECK(platform.edits.empty());

    REQUIRE(reply(c, {{"type", "vary"}, {"partId", "lead"}})["ok"] == true);
    REQUIRE(platform.edits.size() == 1);
    auto [id, req] = platform.edits.back();
    CHECK((req.kind == fb::EditKind::Vary));
    CHECK(req.prompt.empty());
    CHECK(req.partIds == std::vector<std::string>{"lead"});
    CHECK(s.thread().back().text == "Vary Flute");  // the part's name
    auto head = score;
    head["parts"] = json::array();
    c.serviceEvent(id, fb::ScoreHeader{head});
    auto lead = score["parts"][2];
    lead["name"] = "Lead (varied)";
    c.serviceEvent(id, fb::PartDone{lead});
    auto varied = score;
    varied["parts"][2] = lead;
    c.serviceEvent(id, fb::ScoreDone{varied});
    CHECK((s.current()->kind == fb::NodeKind::Vary));
    CHECK(s.current()->partIds == std::vector<std::string>{"lead"});
    CHECK(s.current()->parentId == idea);

    // Add part: a role and no parts; the new part joins the idea.
    REQUIRE(reply(c, {{"type", "addPart"}, {"role", "pad"}, {"prompt", nullptr}})["ok"] == true);
    std::tie(id, req) = platform.edits.back();
    CHECK((req.kind == fb::EditKind::AddPart));
    CHECK(req.partIds == std::vector<std::string>{});
    CHECK((req.role == fb::Role::Pad));
    CHECK(s.thread().back().text == "Add a pad part");
    c.serviceEvent(id, fb::ScoreHeader{head});
    json pad{{"id", "pad"}, {"role", "pad"}, {"name", "Pad"}, {"low", "C3"}, {"high", "C5"}, {"grid", 2}, {"velocity", 64},
             {"blocks", json::array({{{"startBar", 1}, {"endBar", 4}, {"rhythm", "x-------"}, {"voicing", "open"}}})}};
    c.serviceEvent(id, fb::PartDone{pad});
    CHECK(partIdsOf(s.current()->score) == std::vector<std::string>{"keys", "bass", "lead", "drums", "pad"});
    CHECK((s.current()->kind == fb::NodeKind::Edit));
    c.cancelAll();

    // Every part locked: an edit is refused before it's sent.
    for (const char* p : {"keys", "bass", "lead", "drums"})
        reply(c, {{"type", "setPartState"}, {"state", {{"partId", p}, {"muted", false}, {"solo", false}, {"locked", true}, {"density", 0.5}}}});
    const auto sent = platform.edits.size();
    CHECK(reply(c, {{"type", "edit"}, {"prompt", "darker"}, {"partIds", nullptr}})["error"]["code"] == "bad_request");
    CHECK(platform.edits.size() == sent);

    // A question: an answer in the thread, and no node.
    for (const char* p : {"keys", "bass", "lead", "drums"})
        reply(c, {{"type", "setPartState"}, {"state", {{"partId", p}, {"muted", false}, {"solo", false}, {"locked", false}, {"density", 0.5}}}});
    const auto before = s.nodes().size();
    REQUIRE(reply(c, {{"type", "edit"}, {"prompt", "what key is this?"}, {"partIds", nullptr}})["ok"] == true);
    id = platform.edits.back().first;
    c.serviceEvent(id, fb::AssistantMessage{"D dorian."});
    c.serviceEvent(id, fb::ScoreDone{std::nullopt});
    CHECK(s.nodes().size() == before);
    CHECK(s.thread().back().text == "D dorian.");
    CHECK_FALSE(s.thread().back().nodeId.has_value());
}

TEST_CASE("generate: a plan that leaves a role out keeps it out; the sketch only fills while it streams") {
    Session s("inst", "test");
    FakePlatform platform;
    platform.withService = true;
    Controller c(s, platform);
    const auto score = loadScore("example.json");
    REQUIRE(reply(c, generateCommand("no drums"))["ok"] == true);
    const auto stream = platform.plans.back().first;
    c.serviceEvent(stream, headerOf(score));
    c.serviceEvent(stream, fb::PartDone{score["parts"][0]});
    CHECK(partIdsOf(s.current()->score) == std::vector<std::string>{"keys", "sketch-bass", "sketch-melody", "sketch-drums"});
    auto noDrums = score;
    noDrums["parts"].erase(3);
    c.serviceEvent(stream, fb::ScoreDone{noDrums});
    CHECK(partIdsOf(s.current()->score) == std::vector<std::string>{"keys", "bass", "lead"});
}

TEST_CASE("tweak: a local transform makes a tweak node with its parent's seed; locked parts stay; refusals say why") {
    Session s("inst", "test");
    FakePlatform platform;
    Controller c(s, platform);
    auto r = reply(c, {{"type", "tweak"}, {"partId", nullptr}, {"op", "simplify"}, {"amount", nullptr}});
    CHECK(r["error"]["code"] == "bad_request");  // no idea yet

    const auto a = s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 77, 10);
    const auto before = *s.clip();
    REQUIRE(reply(c, {{"type", "setPartState"}, {"state", {{"partId", "bass"}, {"muted", false}, {"solo", false}, {"locked", true}, {"density", 0.5}}}})["ok"] == true);

    // Every unlocked part: octave up. The bass is locked, so it stays where it is.
    r = reply(c, {{"type", "tweak"}, {"partId", nullptr}, {"op", "register"}, {"amount", 1}});
    REQUIRE(r["ok"] == true);
    const auto* t = s.current();
    REQUIRE(t != nullptr);
    CHECK((t->kind == fb::NodeKind::Tweak));
    CHECK((t->parentId == a));
    CHECK(t->seed == 77);
    CHECK((t->partIds == std::vector<std::string>{"keys", "lead"}));
    CHECK(t->score["parts"][1] == s.node(a)->score["parts"][1]);
    const auto& after = *s.clip();
    auto notesOf = [](const fb::Clip& clip, const std::string& id) {
        std::vector<std::tuple<int, int, int, int>> out;
        for (const auto& p : clip.parts)
            if (p.partId == id)
                for (const auto& n : p.notes) out.emplace_back(n.tick, n.dur, n.pitch, n.vel);
        REQUIRE_FALSE(out.empty());
        return out;
    };
    CHECK((notesOf(after, "bass") == notesOf(before, "bass")));
    CHECK((notesOf(after, "drums") == notesOf(before, "drums")));
    CHECK(std::get<2>(notesOf(after, "keys").front()) == std::get<2>(notesOf(before, "keys").front()) + 12);
    CHECK(s.thread().empty());  // a tweak is a card in the lineage, not a message

    // Undo goes back to the idea as it was.
    REQUIRE(reply(c, {{"type", "undo"}})["ok"] == true);
    CHECK(s.current()->id == a);

    // A locked part named on its own is refused, as is a part that isn't there.
    r = reply(c, {{"type", "tweak"}, {"partId", "bass"}, {"op", "simplify"}, {"amount", nullptr}});
    CHECK(r["error"]["code"] == "bad_request");
    CHECK(r["error"]["message"].get<std::string>().find("locked") != std::string::npos);
    r = reply(c, {{"type", "tweak"}, {"partId", "ghost"}, {"op", "simplify"}, {"amount", nullptr}});
    CHECK(r["error"]["code"] == "unknown_part");
    // Transpose moves the key, so it can't leave the locked bass behind.
    r = reply(c, {{"type", "tweak"}, {"partId", nullptr}, {"op", "transpose"}, {"amount", 2}});
    CHECK(r["error"]["code"] == "bad_request");
    CHECK(r["error"]["message"].get<std::string>().find("Finger bass") != std::string::npos);
    // Nothing to change: drums have no register. No node is made.
    const auto nodes = s.nodes().size();
    r = reply(c, {{"type", "tweak"}, {"partId", "drums"}, {"op", "register"}, {"amount", -1}});
    CHECK(r["error"]["code"] == "bad_request");
    CHECK(s.nodes().size() == nodes);

    // With the bass unlocked, transpose takes the whole idea up a tone.
    REQUIRE(reply(c, {{"type", "setPartState"}, {"state", {{"partId", "bass"}, {"muted", false}, {"solo", false}, {"locked", false}, {"density", 0.5}}}})["ok"] == true);
    r = reply(c, {{"type", "tweak"}, {"partId", nullptr}, {"op", "transpose"}, {"amount", 2}});
    REQUIRE(r["ok"] == true);
    CHECK(s.current()->score["context"]["tonic"] == "E");
    CHECK(r["session"]["context"]["tonic"] == "E");

    // One part: simplify, intensify, revoice and humanize each make a node that changes only it.
    for (const auto& [op, amount] : std::vector<std::pair<std::string, json>>{
             {"simplify", nullptr}, {"intensify", nullptr}, {"revoice", nullptr}, {"humanize", 0.9}}) {
        INFO(op);
        const auto parent = s.current()->id;
        r = reply(c, {{"type", "tweak"}, {"partId", "keys"}, {"op", op}, {"amount", amount}});
        REQUIRE(r["ok"] == true);
        CHECK((s.current()->parentId == parent));
        CHECK((s.current()->partIds == std::vector<std::string>{"keys"}));
    }
    // The tweaks survive a save and restore.
    Session restored("inst", "test");
    std::vector<std::string> warnings;
    restored.restore(s.save(), warnings);
    CHECK(warnings.empty());
    CHECK(restored.clip()->parts.size() == s.clip()->parts.size());
    CHECK((notesOf(*restored.clip(), "keys") == notesOf(*s.clip(), "keys")));
}

TEST_CASE("density knob: a live re-render of the part, saved with the project, never a node") {
    Session s("inst", "test");
    FakePlatform platform;
    Controller c(s, platform);
    s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 5, 10);
    auto count = [](const fb::Clip& clip, const std::string& id) {
        for (const auto& p : clip.parts)
            if (p.partId == id) return p.notes.size();
        return std::size_t{0};
    };
    const auto written = count(*s.clip(), "drums");
    const auto keys = count(*s.clip(), "keys");
    const auto nodes = s.nodes().size();
    int changes = 0;
    c.onChanged = [&] { ++changes; };

    auto set = [&](double d) {
        return reply(c, {{"type", "setPartState"}, {"state", {{"partId", "drums"}, {"muted", false}, {"solo", false}, {"locked", false}, {"density", d}}}});
    };
    REQUIRE(set(0.0)["ok"] == true);
    CHECK(count(*s.clip(), "drums") < written);
    CHECK(count(*s.clip(), "keys") == keys);  // only that part
    REQUIRE(set(1.0)["ok"] == true);
    CHECK(count(*s.clip(), "drums") > written);
    CHECK(s.nodes().size() == nodes);
    CHECK(changes == 2);
    // What plays and drags is the knob's version.
    CHECK(count(*c.audition().clip, "drums") == count(*s.clip(), "drums"));
    REQUIRE(reply(c, {{"type", "startDrag"}, {"nodeId", nullptr}, {"partIds", nullptr}, {"splitDrums", false}})["ok"] == true);
    REQUIRE(platform.lastClip);
    CHECK(count(*platform.lastClip, "drums") == count(*s.clip(), "drums"));

    // Saved with the project: the reopened session plays the same.
    Session restored("inst", "test");
    std::vector<std::string> warnings;
    restored.restore(s.save(), warnings);
    CHECK(count(*restored.clip(), "drums") == count(*s.clip(), "drums"));

    REQUIRE(set(0.5)["ok"] == true);
    CHECK(count(*s.clip(), "drums") == written);
}

TEST_CASE("reroll: the part gets a seed of its own in a regenerate node; nothing else changes") {
    Session s("inst", "test");
    FakePlatform platform;
    Controller c(s, platform);
    auto r = reply(c, {{"type", "reroll"}, {"partId", "keys"}});
    CHECK(r["error"]["code"] == "bad_request");  // no idea yet

    const auto a = s.addNode(loadScore("example.json"), fb::NodeKind::Initial, std::nullopt, std::nullopt, 31, 10);
    auto notesOf = [](const fb::Clip& clip, const std::string& id) {
        std::vector<std::tuple<int, int, int, int>> out;
        for (const auto& p : clip.parts)
            if (p.partId == id)
                for (const auto& n : p.notes) out.emplace_back(n.tick, n.dur, n.pitch, n.vel);
        return out;
    };
    const auto before = *s.clip();
    std::set<std::uint64_t> seeds;
    std::set<std::vector<std::tuple<int, int, int, int>>> takes;
    for (int i = 0; i < 4; ++i) {
        r = reply(c, {{"type", "reroll"}, {"partId", "keys"}});
        REQUIRE(r["ok"] == true);
        const auto* n = s.current();
        CHECK((n->kind == fb::NodeKind::Regenerate));
        CHECK(n->seed == 31);
        CHECK((n->partIds == std::vector<std::string>{"keys"}));
        seeds.insert(n->score["parts"][0]["seed"].get<std::uint64_t>());
        takes.insert(notesOf(*s.clip(), "keys"));
        for (const char* other : {"bass", "lead", "drums"}) CHECK((notesOf(*s.clip(), other) == notesOf(before, other)));
        // Only the part's seed differs from the parent's score.
        auto parentScore = s.node(*n->parentId)->score;
        parentScore["parts"][0]["seed"] = n->score["parts"][0]["seed"];
        CHECK(parentScore == n->score);
    }
    CHECK(seeds.size() == 4);
    CHECK(takes.size() >= 3);
    CHECK(r["session"]["nodes"].back()["kind"] == "regenerate");

    // Undo walks back to the take before, then the idea as written.
    for (int i = 0; i < 4; ++i) REQUIRE(reply(c, {{"type", "undo"}})["ok"] == true);
    CHECK(s.current()->id == a);
    CHECK((notesOf(*s.clip(), "keys") == notesOf(before, "keys")));

    // A locked part can't be re-rolled; a missing one is unknown.
    REQUIRE(reply(c, {{"type", "setPartState"}, {"state", {{"partId", "keys"}, {"muted", false}, {"solo", false}, {"locked", true}, {"density", 0.5}}}})["ok"] == true);
    r = reply(c, {{"type", "reroll"}, {"partId", "keys"}});
    CHECK(r["error"]["code"] == "bad_request");
    CHECK(r["error"]["message"].get<std::string>().find("locked") != std::string::npos);
    CHECK(reply(c, {{"type", "reroll"}, {"partId", "ghost"}})["error"]["code"] == "unknown_part");
}
