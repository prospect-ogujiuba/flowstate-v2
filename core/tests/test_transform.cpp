#include <doctest/doctest.h>

#include "flowstate/ir.h"
#include "flowstate/realize.h"
#include "flowstate/theory.h"
#include "flowstate/transform.h"
#include "test_util.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using namespace flowstate;
using nlohmann::json;

namespace {

const char* kFixtures[] = {"example.json", "six_eight.json", "seven_eight.json"};

Realization run(const std::string& score, std::uint64_t seed = 1, bool humanize = true,
                std::map<std::string, double> density = {}) {
    RealizeOptions o;
    o.seed = seed;
    o.humanize = humanize;
    o.density = std::move(density);
    return realizeJson(score, o);
}

const PartRealization& find(const Realization& r, const std::string& id) {
    for (const auto& p : r.parts)
        if (p.id == id) return p;
    FAIL("part not found: " << id);
    return r.parts.front();
}

std::vector<std::string> ids(const std::string& score) {
    std::vector<std::string> out;
    const auto parsed = json::parse(score);
    for (const auto& p : parsed["parts"]) out.push_back(p["id"].get<std::string>());
    return out;
}

bool sameNotes(const PartRealization& a, const PartRealization& b) {
    if (a.notes.size() != b.notes.size()) return false;
    for (std::size_t i = 0; i < a.notes.size(); ++i) {
        const auto &x = a.notes[i], &y = b.notes[i];
        if (x.tick != y.tick || x.dur != y.dur || x.pitch != y.pitch || x.vel != y.vel) return false;
    }
    return true;
}

std::set<Tick> onsets(const PartRealization& p) {
    std::set<Tick> out;
    for (const auto& n : p.notes) out.insert(n.tick);
    return out;
}

}  // namespace

TEST_CASE("tweaks are deterministic and leave the realizer deterministic") {
    const std::string text = readFixture("example.json");
    for (const auto op : {TweakOp::Register, TweakOp::Transpose, TweakOp::Humanize, TweakOp::Simplify,
                          TweakOp::Intensify, TweakOp::Revoice}) {
        CAPTURE(static_cast<int>(op));
        const std::optional<double> amount =
            op == TweakOp::Register ? 1.0 : op == TweakOp::Transpose ? 2.0 : op == TweakOp::Humanize ? 0.8 : 0.0;
        const auto a = tweakJson(text, op, ids(text), amount);
        const auto b = tweakJson(text, op, ids(text), amount);
        REQUIRE_MESSAGE(a.ok(), a.error);
        CHECK(a.score == b.score);
        CHECK(a.changed == b.changed);
        CHECK_FALSE(a.changed.empty());
        const auto r1 = run(a.score, 42), r2 = run(a.score, 42);
        for (std::size_t i = 0; i < r1.parts.size(); ++i) CHECK(sameNotes(r1.parts[i], r2.parts[i]));
    }
}

TEST_CASE("a score without the new part fields, and the knob at 0.5, realize as before") {
    for (const char* name : kFixtures) {
        const std::string text = readFixture(name);
        std::map<std::string, double> half;
        for (const auto& id : ids(text)) half[id] = 0.5;
        auto withFields = json::parse(text);
        for (auto& p : withFields["parts"]) {
            p["density"] = 0.5;
            p["humanize"] = kDefaultHumanize;
        }
        const auto plain = run(text, 7), knob = run(text, 7, true, half), fields = run(withFields.dump(), 7);
        for (std::size_t i = 0; i < plain.parts.size(); ++i) {
            CAPTURE(name);
            CHECK(sameNotes(plain.parts[i], knob.parts[i]));
            CHECK(sameNotes(plain.parts[i], fields.parts[i]));
        }
    }
}

TEST_CASE("register moves pitched parts by octaves and skips drums") {
    const std::string text = readFixture("example.json");
    const auto up = tweakJson(text, TweakOp::Register, ids(text), 1.0);
    REQUIRE(up.ok());
    CHECK(up.changed == std::vector<std::string>{"keys", "bass", "lead"});
    const auto s = json::parse(up.score);
    CHECK(s["parts"][0]["low"] == "A3");
    CHECK(s["parts"][0]["high"] == "D6");
    const auto before = run(text, 1, false), after = run(up.score, 1, false);
    for (const char* id : {"keys", "bass", "lead"}) {
        CAPTURE(id);
        const auto a = find(before, id), b = find(after, id);
        REQUIRE(a.notes.size() == b.notes.size());
        for (std::size_t i = 0; i < a.notes.size(); ++i) {
            CHECK(b.notes[i].pitch == a.notes[i].pitch + 12);
            CHECK(b.notes[i].tick == a.notes[i].tick);
        }
    }
    CHECK(sameNotes(find(before, "drums"), find(after, "drums")));

    const auto drums = tweakJson(text, TweakOp::Register, {"drums"}, 1.0);
    CHECK_FALSE(drums.ok());
    CHECK(drums.score == text);
    CHECK_FALSE(tweakJson(text, TweakOp::Register, ids(text), 0.5).ok());
    CHECK_FALSE(tweakJson(text, TweakOp::Register, ids(text), std::nullopt).ok());
    // A part at the top of MIDI's range can't go further up; the message names it.
    auto top = json::parse(text);
    top["parts"][2]["high"] = "G9";
    const auto stuck = tweakJson(top.dump(), TweakOp::Register, {"lead"}, 1.0);
    CHECK_FALSE(stuck.ok());
    CHECK(stuck.error.find("Flute") != std::string::npos);
}

TEST_CASE("register moves literal notes with the range") {
    const std::string text = readFixture("seven_eight.json");
    const auto down = tweakJson(text, TweakOp::Register, {"lead"}, -1.0);
    REQUIRE(down.ok());
    const auto a = json::parse(text)["parts"][2]["blocks"][0]["notes"];
    const auto b = json::parse(down.score)["parts"][2]["blocks"][0]["notes"];
    REQUIRE(a.size() == b.size());
    REQUIRE_FALSE(a.empty());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(noteNameToMidi(b[i]["pitch"].get<std::string>()) == noteNameToMidi(a[i]["pitch"].get<std::string>()) - 12);
    }
}

TEST_CASE("transpose moves the key, the chords and literal notes, and needs every pitched part") {
    const std::string text = readFixture("example.json");
    const auto up = tweakJson(text, TweakOp::Transpose, ids(text), 2.0);
    REQUIRE(up.ok());
    CHECK(up.changed == std::vector<std::string>{"keys", "bass", "lead"});
    const auto s = json::parse(up.score);
    CHECK(s["context"]["tonic"] == "E");
    std::vector<std::string> symbols;
    for (const auto& c : s["harmony"]) symbols.push_back(c["symbol"].get<std::string>());
    CHECK(symbols == std::vector<std::string>{"Em9", "A13", "F#m7", "Gmaj7", "B7#9"});
    const auto before = run(text, 1, false), after = run(up.score, 1, false);
    CHECK(after.outOfKey.size() == before.outOfKey.size());
    CHECK(sameNotes(find(before, "drums"), find(after, "drums")));
    CHECK(onsets(find(before, "keys")) == onsets(find(after, "keys")));

    // Flat keys spell with flats, in the compact harmony string too, keeping its spacing and bar marks.
    auto compact = json::parse(text);
    compact["harmony"] = "Dm9:4 | G13:4 | Em7:4 | Fmaj7:2 A7#9/E:2";
    const auto flat = tweakJson(compact.dump(), TweakOp::Transpose, ids(text), 3.0);
    REQUIRE(flat.ok());
    const auto f = json::parse(flat.score);
    CHECK(f["context"]["tonic"] == "F");
    CHECK(f["harmony"] == "Fm9:4 | Bb13:4 | Gm7:4 | Abmaj7:2 C7#9/G:2");

    const auto partial = tweakJson(text, TweakOp::Transpose, {"keys", "drums"}, 1.0);
    CHECK_FALSE(partial.ok());
    CHECK(partial.error.find("Finger bass") != std::string::npos);
    CHECK(partial.score == text);
    CHECK_FALSE(tweakJson(text, TweakOp::Transpose, ids(text), 12.0).ok());
}

TEST_CASE("transpose moves literal notes and keeps them in range") {
    const std::string text = readFixture("seven_eight.json");
    const auto up = tweakJson(text, TweakOp::Transpose, ids(text), 5.0);
    REQUIRE(up.ok());
    const auto s = json::parse(up.score);
    CHECK(s["context"]["tonic"] == "D");
    const auto a = json::parse(text)["parts"][2]["blocks"][0]["notes"];
    const auto b = s["parts"][2]["blocks"][0]["notes"];
    for (std::size_t i = 0; i < a.size(); ++i)
        CHECK(noteNameToMidi(b[i]["pitch"].get<std::string>()) == noteNameToMidi(a[i]["pitch"].get<std::string>()) + 5);
    CHECK(run(up.score, 1).warnings.size() == run(text, 1).warnings.size());
}

TEST_CASE("humanize sets the part's amount; 0 is on the grid") {
    const std::string text = readFixture("example.json");
    const auto tight = tweakJson(text, TweakOp::Humanize, {"keys", "drums"}, 0.0);
    REQUIRE(tight.ok());
    CHECK(tight.changed == std::vector<std::string>{"keys", "drums"});
    const auto human = run(tight.score, 5), grid = run(text, 5, false), before = run(text, 5);
    CHECK(sameNotes(find(human, "keys"), find(grid, "keys")));
    CHECK(sameNotes(find(human, "drums"), find(grid, "drums")));
    CHECK(sameNotes(find(human, "bass"), find(before, "bass")));

    const auto loose = tweakJson(text, TweakOp::Humanize, {"keys"}, 1.0);
    REQUIRE(loose.ok());
    CHECK_FALSE(sameNotes(find(run(loose.score, 5), "keys"), find(before, "keys")));
    CHECK_FALSE(tweakJson(text, TweakOp::Humanize, {"keys"}, kDefaultHumanize).ok());
    CHECK_FALSE(tweakJson(text, TweakOp::Humanize, {"keys"}, 1.5).ok());
}

TEST_CASE("simplify and intensify step the written density and change the notes") {
    for (const char* name : kFixtures) {
        CAPTURE(name);
        const std::string text = readFixture(name);
        const auto simple = tweakJson(text, TweakOp::Simplify, ids(text), std::nullopt);
        const auto busy = tweakJson(text, TweakOp::Intensify, ids(text), std::nullopt);
        REQUIRE(simple.ok());
        REQUIRE(busy.ok());
        const auto before = run(text, 3, false), less = run(simple.score, 3, false), more = run(busy.score, 3, false);
        for (const auto& id : simple.changed) {
            CAPTURE(id);
            CHECK(find(less, id).notes.size() < find(before, id).notes.size());
        }
        for (const auto& id : busy.changed) {
            CAPTURE(id);
            CHECK(find(more, id).notes.size() > find(before, id).notes.size());
        }
        CHECK(json::parse(simple.score)["parts"][0]["density"] == 0.25);
    }
    // Twice down reaches 0; a third time can't go further.
    std::string text = readFixture("example.json");
    for (int i = 0; i < 2; ++i) text = tweakJson(text, TweakOp::Simplify, {"bass"}, std::nullopt).score;
    CHECK(json::parse(text)["parts"][1]["density"] == 0.0);
    const auto floor = tweakJson(text, TweakOp::Simplify, {"bass"}, std::nullopt);
    CHECK_FALSE(floor.ok());
    CHECK(floor.error.find("simpler") != std::string::npos);
}

TEST_CASE("density never adds when lower, never removes when higher, and keeps every bar sounding") {
    for (const char* name : kFixtures) {
        const std::string text = readFixture(name);
        const auto partIds = ids(text);
        const auto base = run(text, 11, false);
        std::map<std::string, std::vector<std::size_t>> counts;  // per part, at 0, 0.25, 0.5 (written), 0.75, 1
        for (double d : {0.0, 0.25, 0.75, 1.0}) {
            CAPTURE(name);
            CAPTURE(d);
            std::map<std::string, double> knob;
            for (const auto& id : partIds) knob[id] = d;
            const auto r = run(text, 11, false, knob);
            for (const auto& p : r.parts) {
                CAPTURE(p.id);
                const auto& was = find(base, p.id);
                if (d < 0.5) CHECK(p.notes.size() <= was.notes.size());
                else CHECK(p.notes.size() >= was.notes.size());
                // Every bar that sounded still sounds.
                std::set<Tick> barsBefore, barsAfter;
                for (const auto& n : was.notes) barsBefore.insert(n.tick / base.ticksPerBar);
                for (const auto& n : p.notes) barsAfter.insert(n.tick / r.ticksPerBar);
                for (Tick b : barsBefore) CHECK(barsAfter.count(b) == 1);
                // Within range and the clip, as always.
                for (const auto& n : p.notes) {
                    CHECK(n.tick >= 0);
                    CHECK(n.tick + n.dur <= r.clipEnd());
                    if (p.role != Role::Drums) {
                        CHECK(n.pitch >= p.low);
                        CHECK(n.pitch <= p.high);
                    }
                }
            }
            for (const auto& p : r.parts) {
                if (d == 0.75) counts[p.id].push_back(find(base, p.id).notes.size());
                counts[p.id].push_back(p.notes.size());
            }
        }
        // Monotonic in density.
        for (const auto& [id, c] : counts) {
            CAPTURE(id);
            CHECK(std::is_sorted(c.begin(), c.end()));
        }
        // Literal notes are never thinned: the 7/8 lead's literal notes all survive density 0.
        if (std::string(name) == "seven_eight.json") {
            const auto literal = json::parse(text)["parts"][2]["blocks"][0]["notes"];
            const auto r = run(text, 11, false, {{"lead", 0.0}});
            std::set<int> pitches;
            for (const auto& n : find(r, "lead").notes) pitches.insert(n.pitch);
            for (const auto& n : literal) CHECK(pitches.count(noteNameToMidi(n["pitch"].get<std::string>())) == 1);
        }
    }
}

TEST_CASE("density 0 keeps the strongest onset of each pattern bar") {
    const std::string text = readFixture("example.json");
    const auto r = run(text, 1, false, {{"keys", 0.0}, {"bass", 0.0}});
    // The keys' rhythm ("x--- ..x- .x-- ....") hits beat 1 of each bar; at density 0 that is all that's left.
    std::set<Tick> keys = onsets(find(r, "keys"));
    for (Tick t : keys) CHECK(t % r.ticksPerBar == 0);
    CHECK(keys.size() == static_cast<std::size_t>(r.bars));
}

TEST_CASE("revoice changes the chords' voicing, not their rhythm") {
    const std::string text = readFixture("example.json");
    const auto r = tweakJson(text, TweakOp::Revoice, ids(text), std::nullopt);
    REQUIRE(r.ok());
    CHECK(r.changed == std::vector<std::string>{"keys"});
    CHECK(json::parse(r.score)["parts"][0]["blocks"][0]["voicing"] == "shell");
    const auto a = run(text, 1, false), b = run(r.score, 1, false);
    CHECK(onsets(find(a, "keys")) == onsets(find(b, "keys")));
    CHECK_FALSE(sameNotes(find(a, "keys"), find(b, "keys")));

    const auto pad = tweakJson(readFixture("six_eight.json"), TweakOp::Revoice, {"pad"}, std::nullopt);
    REQUIRE(pad.ok());
    CHECK(json::parse(pad.score)["parts"][1]["blocks"][0]["voicing"].is_string());

    const auto none = tweakJson(text, TweakOp::Revoice, {"bass", "drums"}, std::nullopt);
    CHECK_FALSE(none.ok());
    CHECK_FALSE(tweakJson(text, TweakOp::Revoice, {"nope"}, std::nullopt).ok());
}

TEST_CASE("tweak op names") {
    CHECK(tweakOpFromString("register") == TweakOp::Register);
    CHECK(tweakOpFromString("revoice") == TweakOp::Revoice);
    CHECK_FALSE(tweakOpFromString("loudify"));
}
