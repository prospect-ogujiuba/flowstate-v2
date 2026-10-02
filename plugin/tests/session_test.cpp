// Session, controller, capture and state tests. JUCE-free; runs anywhere core builds.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "session/Capture.h"
#include "session/Controller.h"
#include "session/PluginState.h"
#include "session/Session.h"

#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

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
    std::optional<fb::ErrorInfo> startPlan(const std::string& streamId, const fb::PlanRequest& request) override {
        if (!withService) return Platform::startPlan(streamId, request);
        plans.emplace_back(streamId, request);
        return std::nullopt;
    }
    void cancelStream(const std::string& streamId) override { cancelled.push_back(streamId); }

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
        {"edit", {{"type", "edit"}, {"prompt", "darker"}, {"partIds", nullptr}}},
        {"vary", {{"type", "vary"}, {"partId", partId}}},
        {"addPart", {{"type", "addPart"}, {"role", "pad"}, {"prompt", nullptr}}},
        {"reroll", {{"type", "reroll"}, {"partId", partId}}},
        {"tweak", {{"type", "tweak"}, {"partId", nullptr}, {"op", "simplify"}, {"amount", nullptr}}},
        {"editNotes", {{"type", "editNotes"}, {"partId", partId}, {"remove", json::array()}, {"add", json::array()}}},
        {"capture", {{"type", "generate"}, {"prompt", ""}, {"roles", nullptr}, {"count", 1}, {"capture", {{"bars", 4}, {"intent", "continue"}}}}},
        {"apiKey", {{"type", "setApiKey"}, {"provider", "openai"}, {"key", "sk-test"}}},
    };
    for (const auto& [feature, command] : commands) {
        INFO(feature);
        const auto r = reply(c, command);
        CHECK(r["error"]["code"] == "unavailable");
        REQUIRE(gaps.count(feature) == 1);
        CHECK(r["error"]["message"] == gaps[feature]);
    }
    // Accepted, but without effect yet: the UI marks it too. Lock works since the planner keeps locked parts.
    CHECK(gaps.count("density") == 1);
    CHECK(gaps.count("lock") == 0);
}

TEST_CASE("an API key sent over the bridge is never persisted or echoed") {
    Session s("inst", "test");
    FakePlatform platform;
    Controller c(s, platform);
    const std::string key = "sk-test-SECRET-123";
    const auto out = c.handleJson(json{{"type", "setApiKey"}, {"provider", "anthropic"}, {"key", key}}.dump());
    CHECK(out.find(key) == std::string::npos);
    CHECK(encodeState({s.save(), 960, 600}).find(key) == std::string::npos);

    // Also not echoed from a malformed command.
    const auto bad = c.handleJson(json{{"type", "setApiKey"}, {"provider", 5}, {"key", key}}.dump());
    CHECK(bad.find(key) == std::string::npos);
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
    CHECK(h.barsAvailable(10.0 + 3 * bar + 0.1, bar) == 4);

    h.trim(10.0 + 64 * bar + 1.0, bar);  // the first note is now older than 64 bars
    CHECK(h.size() == 1);                // only the note-off at 11.0 survives
    CHECK(h.barsAvailable(10.0 + 64 * bar + 1.0, bar) == 0);  // no note-on left

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
    CHECK(s.nodes().empty());  // nothing plays until a part lands

    c.serviceEvent(streamId, fb::PartStarted{"keys", fb::Role::Chords});
    c.serviceEvent(streamId, fb::PartDone{score["parts"][0]});
    REQUIRE(s.nodes().size() == 1);
    const auto nodeId = s.nodes()[0].id;
    CHECK(s.current()->id == nodeId);
    REQUIRE(s.clip().has_value());
    CHECK(s.clip()->parts.size() == 1);
    CHECK(events.list.back()["type"] == "partReady");
    CHECK(events.list.back()["partId"] == "keys");

    c.serviceEvent(streamId, fb::PartDone{score["parts"][1]});
    c.serviceEvent(streamId, fb::PartDone{score["parts"][0]});  // a repaired part replaces the first
    CHECK(s.nodes().size() == 1);
    CHECK(s.clip()->parts.size() == 2);
    CHECK(c.view().generations[0].partsDone == std::vector<std::string>{"keys", "bass"});

    c.serviceEvent(streamId, fb::AssistantMessage{"A slow neo-soul loop."});
    c.serviceEvent(streamId, fb::ScoreDone{score});
    CHECK(s.node(nodeId)->score == score);  // the authoritative score
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
    CHECK(s.nodes().size() == 3);
    CHECK(s.current()->id == playing);  // a later variation doesn't take over
    CHECK(s.node(s.nodes()[2].id)->parentId == parent);
    CHECK(s.node(s.nodes()[2].id)->seed != s.node(playing)->seed);

    c.serviceEvent(one, fb::ServiceError{{fb::ErrorCode::Provider, "upstream 502"}});
    CHECK(s.nodes().size() == 2);  // its partial idea is gone
    c.serviceEnded(three, fb::ErrorInfo{fb::ErrorCode::Network, "reset"});
    CHECK(c.generating());
    c.serviceEvent(two, fb::ScoreDone{score});

    CHECK_FALSE(c.generating());
    const auto types = events.types();
    CHECK(types[types.size() - 2] == "generationDone");
    CHECK(events.list[types.size() - 2]["nodeIds"] == json::array({playing}));
    CHECK(types.back() == "notice");
    CHECK(s.current()->id == playing);
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
    CHECK(s.nodes().empty());
    CHECK(s.current() == nullptr);
    CHECK_FALSE(s.clip().has_value());
    CHECK(events.list.back()["type"] == "generationFailed");
    CHECK(events.list.back()["error"]["code"] == "cancelled");
    CHECK(cancelled["session"]["generations"].empty());

    // Whatever was still in flight is ignored.
    c.serviceEvent(stream, fb::PartDone{score["parts"][1]});
    c.serviceEvent(stream, fb::ScoreDone{score});
    CHECK(s.nodes().empty());

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
    CHECK(s.nodes().empty());

    // A question answered with text: done without a score, no node, an answer in the thread.
    reply(c, generateCommand("what swing suits house?"));
    id = platform.plans.back().first;
    c.serviceEvent(id, fb::AssistantMessage{"Around 55 to 58 percent."});
    c.serviceEvent(id, fb::ScoreDone{std::nullopt});
    CHECK(events.list.back()["type"] == "generationDone");
    CHECK(events.list.back()["nodeIds"].empty());
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

    // "Use what I just played" waits for the planner's reference support.
    auto r = reply(c, {{"type", "generate"}, {"prompt", ""}, {"roles", nullptr}, {"count", 1},
                       {"capture", {{"bars", 4}, {"intent", "continue"}}}});
    CHECK(r["error"]["code"] == "unavailable");

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
