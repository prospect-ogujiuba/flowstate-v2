#include <doctest/doctest.h>

#include "flowstate/ir.h"
#include "flowstate/realize.h"
#include "flowstate/theory.h"
#include "flowstate/transform.h"
#include "test_util.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using namespace flowstate;
using nlohmann::json;

namespace {

const char* kFixtures[] = {"example.json", "six_eight.json", "seven_eight.json"};

RealizeOptions opts(std::uint64_t seed = 7, std::map<std::string, double> density = {}) {
    RealizeOptions o;
    o.seed = seed;
    o.density = std::move(density);
    return o;
}

PartRealization part(const Realization& r, const std::string& id) {
    for (const auto& p : r.parts)
        if (p.id == id) return p;
    FAIL("part not found: " << id);
    return r.parts.front();
}

ClipNoteEdit edit(const NoteEvent& n) { return {n.tick, n.dur, n.pitch, n.vel}; }

bool has(const PartRealization& p, const ClipNoteEdit& e, bool exact = true) {
    return std::any_of(p.notes.begin(), p.notes.end(), [&](const NoteEvent& n) {
        return n.tick == e.tick && n.pitch == e.pitch && (!exact || (n.dur == e.dur && n.vel == e.vel));
    });
}

std::vector<NoteEvent> without(std::vector<NoteEvent> notes, const NoteEvent& gone) {
    notes.erase(std::find_if(notes.begin(), notes.end(), [&](const NoteEvent& n) {
        return n.tick == gone.tick && n.pitch == gone.pitch && n.dur == gone.dur;
    }));
    return notes;
}

bool same(const std::vector<NoteEvent>& a, const std::vector<NoteEvent>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].tick != b[i].tick || a[i].dur != b[i].dur || a[i].pitch != b[i].pitch || a[i].vel != b[i].vel) return false;
    return true;
}

}  // namespace

TEST_CASE("touch: a removed note goes, and nothing else changes") {
    for (const char* f : kFixtures) {
        CAPTURE(f);
        const std::string text = readFixture(f);
        const auto before = realizeJson(text, opts());
        for (const auto& p : before.parts) {
            CAPTURE(p.id);
            REQUIRE_FALSE(p.notes.empty());
            const NoteEvent gone = p.notes[p.notes.size() / 2];
            const auto result = editNotesJson(text, p.id, {edit(gone)}, {}, opts());
            REQUIRE_MESSAGE(result.ok(), result.error);
            const auto after = realizeJson(result.score, opts());
            CHECK(same(part(after, p.id).notes, without(p.notes, gone)));
            for (const auto& other : before.parts)
                if (other.id != p.id) CHECK(same(part(after, other.id).notes, other.notes));
        }
    }
}

TEST_CASE("touch: an added note plays exactly as written, in or out of key") {
    const std::string text = readFixture("example.json");
    const auto before = realizeJson(text, opts());
    // Bar 3, beat 2.5, a 16th long, a note outside D dorian.
    const ClipNoteEdit added{2 * 3840 + 1440, 240, 63, 77};
    const auto result = editNotesJson(text, "lead", {}, {added}, opts());
    REQUIRE_MESSAGE(result.ok(), result.error);
    const auto after = realizeJson(result.score, opts());
    CHECK(has(part(after, "lead"), added));
    CHECK(std::none_of(after.outOfKey.begin(), after.outOfKey.end(),
                       [&](const OutOfKeyNote& n) { return n.part == "lead" && n.tick == added.tick; }));
    for (const char* id : {"keys", "bass", "drums"}) CHECK(same(part(after, id).notes, part(before, id).notes));

    const ClipNoteEdit hat{3840 + 120, 60, 42, 50};
    const auto drums = editNotesJson(text, "drums", {}, {hat}, opts());
    REQUIRE(drums.ok());
    const auto drumsAfter = realizeJson(drums.score, opts());
    const auto& d = part(drumsAfter, "drums");
    const auto it = std::find_if(d.notes.begin(), d.notes.end(), [&](const NoteEvent& n) { return n.tick == hat.tick && n.pitch == 42; });
    REQUIRE(it != d.notes.end());
    CHECK(it->sublane == "hats");
}

TEST_CASE("touch: removing a note an earlier edit added takes the addition back") {
    const std::string text = readFixture("example.json");
    const ClipNoteEdit added{3840 + 960, 480, 74, 90};
    const auto first = editNotesJson(text, "lead", {}, {added}, opts());
    REQUIRE(first.ok());
    const auto second = editNotesJson(first.score, "lead", {added}, {}, opts());
    REQUIRE_MESSAGE(second.ok(), second.error);
    CHECK(json::parse(second.score) == json::parse(text));  // as written again: no `touch` left
}

TEST_CASE("touch: a move is a removal plus an addition, and edits stack") {
    const std::string text = readFixture("example.json");
    const auto before = realizeJson(text, opts());
    const auto& bass = part(before, "bass").notes;
    const NoteEvent a = bass[1], b = bass[bass.size() - 2];
    const ClipNoteEdit moved{a.tick, a.dur, a.pitch + 2, a.vel};
    const auto first = editNotesJson(text, "bass", {edit(a)}, {moved}, opts());
    REQUIRE_MESSAGE(first.ok(), first.error);
    const auto second = editNotesJson(first.score, "bass", {edit(b)}, {}, opts());
    REQUIRE_MESSAGE(second.ok(), second.error);
    const auto realized = realizeJson(second.score, opts());
    const auto& after = part(realized, "bass");
    CHECK(has(after, moved));
    CHECK_FALSE(has(after, edit(a)));
    CHECK_FALSE(has(after, edit(b)));
    CHECK(after.notes.size() == bass.size() - 1);
    CHECK(editNotesJson(text, "bass", {edit(a)}, {moved}, opts()).score == first.score);  // deterministic
}

TEST_CASE("touch: edits survive the density knob and a re-roll") {
    const std::string text = readFixture("example.json");
    const auto before = realizeJson(text, opts());
    const auto& bass = part(before, "bass").notes;
    const NoteEvent gone = bass[2];
    const ClipNoteEdit added{3 * 3840 + 2880 + 240, 240, 50, 101};
    const auto result = editNotesJson(text, "bass", {edit(gone)}, {added}, opts());
    REQUIRE(result.ok());

    auto rerolled = json::parse(result.score);
    for (auto& p : rerolled["parts"])
        if (p["id"] == "bass") p["seed"] = 123456;
    const Tick window = 960 / 8;
    for (const auto& r : {realizeJson(result.score, opts(7, {{"bass", 0.9}})), realizeJson(result.score, opts(7, {{"bass", 0.2}})),
                          realizeJson(rerolled.dump(), opts())}) {
        const auto& b = part(r, "bass");
        CHECK(has(b, added));
        CHECK(std::none_of(b.notes.begin(), b.notes.end(), [&](const NoteEvent& n) {
            return n.pitch == gone.pitch && n.tick > gone.tick - window && n.tick < gone.tick + window;
        }));
    }
}

TEST_CASE("touch: in a monophonic part an added note wins") {
    const std::string text = readFixture("example.json");
    const auto before = realizeJson(text, opts());
    const auto& bass = part(before, "bass").notes;
    const auto held = std::max_element(bass.begin(), bass.end(), [](const NoteEvent& x, const NoteEvent& y) { return x.dur < y.dur; });
    REQUIRE(held->dur >= 480);
    const ClipNoteEdit inside{held->tick + held->dur / 2, 120, held->pitch + 5, 90};
    const auto result = editNotesJson(text, "bass", {}, {inside}, opts());
    REQUIRE(result.ok());
    const auto realized = realizeJson(result.score, opts());
    const auto& after = part(realized, "bass").notes;
    for (std::size_t i = 1; i < after.size(); ++i) CHECK(after[i - 1].tick + after[i - 1].dur <= after[i].tick);
    CHECK(has(part(realized, "bass"), inside));
}

TEST_CASE("touch: refusals") {
    const std::string text = readFixture("example.json");
    const auto before = realizeJson(text, opts());
    const auto& lead = part(before, "lead").notes;
    CHECK_FALSE(editNotesJson(text, "nope", {edit(lead[0])}, {}, opts()).ok());
    CHECK_FALSE(editNotesJson(text, "lead", {}, {}, opts()).ok());
    ClipNoteEdit stale = edit(lead[0]);
    stale.tick += 7;
    const auto r = editNotesJson(text, "lead", {stale}, {}, opts());
    CHECK_FALSE(r.ok());
    CHECK(r.score == text);
    CHECK_FALSE(editNotesJson(text, "lead", {}, {{4 * 3840, 100, 60, 90}}, opts()).ok());  // past the clip
    CHECK_FALSE(editNotesJson(text, "lead", {}, {{0, 100, 60, 0}}, opts()).ok());          // velocity 0
}

TEST_CASE("touch: an empty touch realizes like none") {
    for (const char* f : kFixtures) {
        const std::string text = readFixture(f);
        auto withEmpty = json::parse(text);
        for (auto& p : withEmpty["parts"]) p["touch"] = {{"remove", json::array()}, {"add", json::array()}};
        const auto a = realizeJson(text, opts()), b = realizeJson(withEmpty.dump(), opts());
        for (std::size_t i = 0; i < a.parts.size(); ++i) CHECK(same(a.parts[i].notes, b.parts[i].notes));
    }
}
