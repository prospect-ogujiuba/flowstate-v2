// Session, controller, capture and state tests. JUCE-free; runs anywhere core builds.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "session/Capture.h"
#include "session/Controller.h"
#include "session/PluginState.h"
#include "session/Session.h"

#include <fstream>
#include <iterator>
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
