#include <doctest/doctest.h>

#include "flowstate/output.h"
#include "flowstate/realize.h"
#include "flowstate/smf.h"
#include "flowstate/theory.h"
#include "test_util.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using namespace flowstate;
using nlohmann::json;

#ifndef GOLDEN_EXAMPLE_CHECKSUM
#define GOLDEN_EXAMPLE_CHECKSUM 6977882849122429349ULL
#endif

namespace {

const char* kFixtures[] = {"example.json", "six_eight.json", "seven_eight.json"};

// Minimal valid score with every key present; tests patch it.
json baseScore() {
    return json::parse(R"({
      "ir": "flowstate.score.v0", "title": "t",
      "context": {"tempo": 120, "meterNumerator": 4, "meterDenominator": 4, "tonic": "C", "mode": "major",
                  "bars": 2, "swing": 0, "style": []},
      "form": [], "harmony": [{"bar": 1, "beat": 1, "beats": 4, "symbol": "C"},
                              {"bar": 2, "beat": 1, "beats": 4, "symbol": "G"}],
      "motifs": [], "parts": []
    })");
}

json block(int start, int end) {
    return json{{"startBar", start}, {"endBar", end}, {"rhythm", nullptr}, {"voicing", nullptr},
                {"arpPattern", nullptr}, {"motif", nullptr}, {"transforms", nullptr}, {"repeatEvery", nullptr},
                {"drums", nullptr}, {"fill", nullptr}, {"notes", nullptr}, {"articulation", nullptr}};
}

json part(const std::string& id, const std::string& role, const std::string& low, const std::string& high,
          int grid, json blocks) {
    return json{{"id", id}, {"role", role}, {"name", id}, {"low", low}, {"high", high},
                {"grid", grid}, {"velocity", 90}, {"blocks", blocks}};
}

Realization run(const json& score, std::uint64_t seed = 1, bool humanize = true) {
    RealizeOptions o;
    o.seed = seed;
    o.humanize = humanize;
    return realizeJson(score.dump(), o);
}

PartRealization find(const Realization& r, const std::string& id) {
    for (const auto& p : r.parts)
        if (p.id == id) return p;
    FAIL("part not found: " << id);
    return r.parts.front();
}

}  // namespace

TEST_CASE("clip length, range and monophony properties over fixtures and seeds") {
    for (const char* name : kFixtures) {
        const std::string text = readFixture(name);
        REQUIRE_FALSE(text.empty());
        for (std::uint64_t seed : {1ULL, 2ULL, 99ULL, 123456789ULL}) {
            CAPTURE(name);
            CAPTURE(seed);
            RealizeOptions o;
            o.seed = seed;
            Realization r = realizeJson(text, o);
            const Tick end = r.clipEnd();
            CHECK(end == static_cast<Tick>(r.bars) * r.ticksPerBar);
            for (const auto& p : r.parts) {
                CAPTURE(p.id);
                CHECK_FALSE(p.notes.empty());
                for (std::size_t i = 0; i < p.notes.size(); ++i) {
                    const auto& n = p.notes[i];
                    CHECK(n.tick >= 0);
                    CHECK(n.tick < end);
                    CHECK(n.dur >= 1);
                    CHECK(n.tick + n.dur <= end);
                    CHECK(n.vel >= 1);
                    CHECK(n.vel <= 127);
                    if (p.role != Role::Drums) {
                        CHECK(n.pitch >= p.low);
                        CHECK(n.pitch <= p.high);
                    } else {
                        CHECK_FALSE(n.sublane.empty());
                    }
                    if (i > 0) {
                        const auto& prev = p.notes[i - 1];
                        CHECK((prev.tick < n.tick || (prev.tick == n.tick && prev.pitch < n.pitch)));
                    }
                    if (isPitchedMonophonic(p.role) && i + 1 < p.notes.size()) {
                        CHECK(n.tick + n.dur <= p.notes[i + 1].tick);
                        CHECK(n.tick < p.notes[i + 1].tick);
                    }
                }
                if (p.role == Role::Drums) CHECK(p.channel == 9);
                else CHECK(p.channel != 9);
            }
        }
    }
}

TEST_CASE("determinism: same IR + seed gives byte-identical outputs") {
    for (const char* name : kFixtures) {
        CAPTURE(name);
        const std::string text = readFixture(name);
        RealizeOptions o;
        o.seed = 42;
        Realization a = realizeJson(text, o);
        Realization b = realizeJson(text, o);
        CHECK(notesJson(a) == notesJson(b));
        CHECK(reportJson(a) == reportJson(b));
        CHECK(writeSmf(a) == writeSmf(b));
        o.seed = 43;
        Realization c = realizeJson(text, o);
        CHECK(notesJson(a) != notesJson(c));  // humanize depends on the seed
    }
}

TEST_CASE("golden: spec example realizes to a stable result") {
    RealizeOptions o;
    o.seed = 1;
    Realization r = realizeJson(readFixture("example.json"), o);
    CHECK(r.warnings.empty());
    CHECK(r.outOfKey.empty());
    std::map<std::string, std::size_t> counts;
    for (const auto& p : r.parts) counts[p.id] = p.notes.size();
    CHECK(counts["keys"] == 48);  // 12 hits x 4-voice rootless voicings
    CHECK(counts["bass"] == 16);
    CHECK(counts["lead"] == 16);
    CHECK(counts["drums"] == 65);
    MESSAGE("example checksum: " << notesChecksum(r));
    CHECK(notesChecksum(r) == GOLDEN_EXAMPLE_CHECKSUM);

    // Without humanize the grid is exact and musical content is checkable.
    o.humanize = false;
    Realization q = realizeJson(readFixture("example.json"), o);
    const auto& keys = find(q, "keys");
    std::vector<int> first;
    for (const auto& n : keys.notes)
        if (n.tick == 0) first.push_back(n.pitch);
    CHECK(first == std::vector<int>{53, 57, 60, 64});  // Dm9 rootless A form: F A C E
    const auto& bass = find(q, "bass");
    CHECK(bass.notes.front().pitch == 38);  // D2
    CHECK(bass.notes.front().dur == 864);   // R--- at 90% gate
    const auto& lead = find(q, "lead");
    CHECK(lead.notes[0].pitch == 81);  // degree 5 of D dorian above the D5 centre
    CHECK(lead.notes[1].pitch == 79);
    CHECK(lead.notes[3].pitch == 74);
}

TEST_CASE("6/8 fixture: bar math, pulses and a tom fill in the last bar") {
    RealizeOptions o;
    o.humanize = false;
    Realization r = realizeJson(readFixture("six_eight.json"), o);
    CHECK(r.ticksPerBar == 2880);
    CHECK(r.clipEnd() == 11520);
    const auto& kit = find(r, "kit");
    int toms = 0;
    for (const auto& n : kit.notes) {
        if (n.sublane == "toms") {
            ++toms;
            CHECK(n.tick >= 3 * 2880 + 1440);
        }
    }
    CHECK(toms >= 3);
    // Kick on both dotted-quarter pulses of bar 1.
    std::vector<Tick> kicks;
    for (const auto& n : kit.notes)
        if (n.pitch == 36 && n.tick < 2880) kicks.push_back(n.tick);
    CHECK(kicks == std::vector<Tick>{0, 1440});
    // Bass uses the slash bass of C/G as its root.
    const auto& bass = find(r, "bass");
    bool gOnBar2 = false;
    for (const auto& n : bass.notes)
        if (n.tick == 2880) gOnBar2 = mod12(n.pitch) == 7;
    CHECK(gOnBar2);
}

TEST_CASE("invalid input fails clearly") {
    CHECK_THROWS_AS(realizeJson("{not json"), IrError);
    json s = baseScore();
    s["ir"] = "flowstate.score.v1";
    CHECK_THROWS_AS(run(s), IrError);
    json t = baseScore();
    t["context"]["mode"] = "ionian-ish";
    CHECK_THROWS_AS(run(t), IrError);
    json u = baseScore();
    u.erase("parts");
    CHECK_THROWS_AS(run(u), IrError);
}

TEST_CASE("chords rest in harmony gaps and re-strike at chord changes inside holds") {
    json s = baseScore();
    s["harmony"] = json::array({json{{"bar", 1}, {"beat", 1}, {"beats", 2}, {"symbol", "C"}},
                                json{{"bar", 1}, {"beat", 4}, {"beats", 5}, {"symbol", "F"}}});
    json b = block(1, 2);
    b["rhythm"] = "x---------------";  // one long hold per bar
    b["voicing"] = "close";
    s["parts"].push_back(part("keys", "chords", "C3", "C5", 4, json::array({b})));
    Realization r = run(s, 1, false);
    const auto& keys = find(r, "keys");
    std::vector<Tick> onsets;
    for (const auto& n : keys.notes)
        if (onsets.empty() || onsets.back() != n.tick) onsets.push_back(n.tick);
    // C at 0 (cut at beat 3 by the gap), F re-struck at beat 4, then bar 2.
    CHECK(onsets == std::vector<Tick>{0, 2880, 3840});
    for (const auto& n : keys.notes)
        if (n.tick == 0) CHECK(n.dur <= 1920);
}

TEST_CASE("bass tokens: chord tones, octave, ghost and chromatic approach") {
    json s = baseScore();
    s["harmony"] = json::array({json{{"bar", 1}, {"beat", 1}, {"beats", 4}, {"symbol", "C7"}},
                                json{{"bar", 2}, {"beat", 1}, {"beats", 4}, {"symbol", "F/A"}}});
    json b = block(1, 2);
    b["rhythm"] = "R.3.5.7.8.g...a.";
    s["parts"].push_back(part("bass", "bass", "E1", "G3", 4, json::array({b})));
    Realization r = run(s, 1, false);
    const auto& bass = find(r, "bass");
    REQUIRE(bass.notes.size() == 14);
    std::vector<int> pcs;
    for (int i = 0; i < 6; ++i) pcs.push_back(mod12(bass.notes[static_cast<std::size_t>(i)].pitch));
    CHECK(pcs == std::vector<int>{0, 4, 7, 10, 0, 0});  // R 3 5 b7 8 g over C7
    CHECK(bass.notes[4].pitch == bass.notes[0].pitch + 12);
    CHECK(bass.notes[5].vel < bass.notes[0].vel / 2 + 5);  // ghost
    CHECK(mod12(bass.notes[7].pitch) == 9);                 // slash bass A is the root of F/A
    CHECK(std::abs(bass.notes[6].pitch - bass.notes[7].pitch) == 1);
    CHECK(r.outOfKey.empty());
}

TEST_CASE("motif transforms: transpose, invert, retrograde, displace, augment, octave") {
    json s = baseScore();
    s["context"]["bars"] = 1;
    s["motifs"] = json::array({json{{"id", "m"},
                                    {"notes", json::array({
                                                  json{{"degree", 1}, {"octave", 0}, {"alter", 0}, {"beat", 0}, {"beats", 1}, {"accent", false}},
                                                  json{{"degree", 3}, {"octave", 0}, {"alter", 0}, {"beat", 1}, {"beats", 1}, {"accent", false}},
                                                  json{{"degree", 5}, {"octave", 0}, {"alter", 1}, {"beat", 2}, {"beats", 1}, {"accent", true}},
                                              })}}});
    auto pitches = [&](const std::vector<std::string>& transforms, std::vector<Tick>* ticks = nullptr) {
        json t = s;
        json b = block(1, 1);
        b["motif"] = "m";
        b["repeatEvery"] = 0;
        b["transforms"] = transforms;
        t["parts"] = json::array({part("lead", "melody", "C3", "C6", 4, json::array({b}))});
        Realization r = run(t, 1, false);
        std::vector<int> out;
        for (const auto& n : r.parts[0].notes) {
            out.push_back(n.pitch);
            if (ticks) ticks->push_back(n.tick);
        }
        return out;
    };
    // Centre: C3..C6 midpoint 66; C4 and C5 are equally near, the lower wins.
    CHECK(pitches({}) == std::vector<int>{60, 64, 68});
    CHECK(pitches({"transpose:+1"}) == std::vector<int>{62, 65, 70});
    CHECK(pitches({"invert"}) == std::vector<int>{60, 57, 52});  // C, A3, F3 minus alter -> E3
    CHECK(pitches({"retrograde"}) == std::vector<int>{68, 64, 60});
    CHECK(pitches({"octave:+1"}) == std::vector<int>{72, 76, 80});
    std::vector<Tick> ticks;
    pitches({"displace:+0.5"}, &ticks);
    CHECK(ticks == std::vector<Tick>{480, 1440, 2400});
    ticks.clear();
    pitches({"augment"}, &ticks);
    CHECK(ticks == std::vector<Tick>{0, 1920});  // the third note would start at the block end
    ticks.clear();
    pitches({"diminish"}, &ticks);
    CHECK(ticks == std::vector<Tick>{0, 480, 960});
}

TEST_CASE("melody sketch: rhythm only gives a stepwise, chord-anchored line") {
    json s = baseScore();
    s["context"]["bars"] = 4;
    s["harmony"] = json::array({json{{"bar", 1}, {"beat", 1}, {"beats", 8}, {"symbol", "C"}},
                                json{{"bar", 3}, {"beat", 1}, {"beats", 8}, {"symbol", "Am"}}});
    json b = block(1, 4);
    b["rhythm"] = "x.x.x.x.X.x.x.x.";
    s["parts"].push_back(part("lead", "melody", "C4", "C6", 4, json::array({b})));
    Realization r = run(s, 5, false);
    const auto& lead = find(r, "lead");
    REQUIRE(lead.notes.size() == 32);
    const Scale c = makeScale(0, Mode::Major);
    int leaps = 0;
    for (std::size_t i = 0; i < lead.notes.size(); ++i) {
        CHECK(c.contains(lead.notes[i].pitch));
        if (i > 0 && std::abs(lead.notes[i].pitch - lead.notes[i - 1].pitch) > 5) ++leaps;
        // Beats land on chord tones.
        if (lead.notes[i].tick % 960 == 0) {
            const Tick t = lead.notes[i].tick;
            const Chord ch = *parseChord(t < 7680 ? "C" : "Am").chord;
            CHECK(ch.containsPc(lead.notes[i].pitch));
        }
    }
    CHECK(leaps <= 2);
}

TEST_CASE("drums: GM mapping, accents louder, ghosts quieter, swing on odd 16ths") {
    json s = baseScore();
    s["context"]["bars"] = 1;
    s["context"]["swing"] = 0.5;
    json b = block(1, 1);
    b["drums"] = json::array({json{{"voice", "kick"}, {"steps", "x...x...x...x..."}},
                              json{{"voice", "snare"}, {"steps", "....X..g....x..."}},
                              json{{"voice", "closed_hat"}, {"steps", "xxxxxxxxxxxxxxxx"}},
                              json{{"voice", "cowbell"}, {"steps", "x..............."}}});
    s["parts"].push_back(part("kit", "drums", "C1", "C6", 4, json::array({b})));
    Realization r = run(s, 1, false);
    const auto& kit = find(r, "kit");
    std::map<int, std::vector<NoteEvent>> byPitch;
    for (const auto& n : kit.notes) byPitch[n.pitch].push_back(n);
    CHECK(byPitch[36].size() == 4);
    CHECK(byPitch[56].size() == 1);
    CHECK(byPitch[56][0].sublane == "aux_kit");
    CHECK(byPitch[42][0].sublane == "hats");
    const auto& snares = byPitch[38];
    REQUIRE(snares.size() == 3);
    CHECK(snares[0].vel > snares[2].vel);        // accent
    CHECK(snares[1].vel < snares[2].vel * 0.6);  // ghost
    // Hats: step 1 (odd 16th) swung by 0.5 * 240 = 120 ticks.
    CHECK(byPitch[42][1].tick == 360);
    CHECK(byPitch[42][2].tick == 480);
    CHECK(byPitch[42][0].vel > byPitch[42][1].vel);  // downbeat hat louder than a 16th
}

TEST_CASE("energy scales velocity") {
    json s = baseScore();
    s["form"] = json::array({json{{"name", "A"}, {"startBar", 1}, {"bars", 1}, {"energy", 0.2}},
                             json{{"name", "B"}, {"startBar", 2}, {"bars", 1}, {"energy", 1.0}}});
    json b = block(1, 2);
    b["rhythm"] = "x...............";
    s["parts"].push_back(part("keys", "pad", "C3", "C5", 4, json::array({b})));
    Realization r = run(s, 1, false);
    const auto& pad = find(r, "keys");
    int quiet = 0, loud = 0;
    for (const auto& n : pad.notes) (n.tick < 3840 ? quiet : loud) = n.vel;
    CHECK(loud > quiet + 20);
}

TEST_CASE("literal notes: clamped to the clip, range-folded, justified") {
    json s = baseScore();
    json b = block(1, 2);
    b["rhythm"] = "x...............";
    b["notes"] = json::array({json{{"bar", 2}, {"beat", 4}, {"beats", 4}, {"pitch", "C#7"}, {"velocity", 100}}});
    s["parts"].push_back(part("keys", "chords", "C3", "C5", 4, json::array({b})));
    Realization r = run(s, 1, false);
    const auto& keys = find(r, "keys");
    bool found = false;
    for (const auto& n : keys.notes) {
        if (n.tick == 3840 + 2880) {
            found = true;
            CHECK(n.pitch == 61);           // C#7 folded to C#4
            CHECK(n.tick + n.dur == 7680);  // clamped to the clip end
        }
    }
    CHECK(found);
    CHECK(r.outOfKey.empty());  // literal chromatic note is intentional
}

TEST_CASE("wrong-length patterns and overlaps produce warnings, not failures") {
    json s = baseScore();
    json a = block(1, 2);
    a["rhythm"] = "x.x";
    json b = block(2, 2);
    b["rhythm"] = "x---------------";
    s["parts"].push_back(part("keys", "chords", "C3", "C5", 4, json::array({a, b})));
    Realization r = run(s, 1, false);
    CHECK(r.warnings.size() >= 2);
    // Bar 2 belongs to the later block: a single long hit.
    std::vector<Tick> onsets;
    for (const auto& n : find(r, "keys").notes)
        if (n.tick >= 3840 && (onsets.empty() || onsets.back() != n.tick)) onsets.push_back(n.tick);
    CHECK(onsets == std::vector<Tick>{3840});
}

TEST_CASE("arp patterns walk the chord") {
    json s = baseScore();
    s["context"]["bars"] = 1;
    s["harmony"] = json::array({json{{"bar", 1}, {"beat", 1}, {"beats", 4}, {"symbol", "Cmaj7"}}});
    auto run1 = [&](const char* pattern) {
        json t = s;
        json b = block(1, 1);
        b["rhythm"] = "xxxxxxxx";
        b["arpPattern"] = pattern;
        t["parts"] = json::array({part("arp", "arp", "C4", "C6", 2, json::array({b}))});
        std::vector<int> out;
        const Realization r = run(t, 3, false);
        for (const auto& n : r.parts[0].notes) out.push_back(n.pitch);
        return out;
    };
    auto up = run1("up");
    REQUIRE(up.size() == 8);
    CHECK(std::is_sorted(up.begin(), up.begin() + 4));
    auto down = run1("down");
    CHECK(std::is_sorted(down.begin(), down.begin() + 4, std::greater<int>()));
    auto ud = run1("updown");
    CHECK(ud[0] < ud[1]);
    auto rnd = run1("random");
    for (std::size_t i = 1; i < rnd.size(); ++i) CHECK(rnd[i] != rnd[i - 1]);
    auto alberti = run1("chord_tones");
    CHECK(alberti[1] == alberti[3]);
    for (int p : alberti) CHECK(parseChord("Cmaj7").chord->containsPc(p));
}
