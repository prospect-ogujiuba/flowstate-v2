#include <doctest/doctest.h>

#include "flowstate/voicing.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace flowstate;

namespace {

Chord chord(const std::string& s) { return *parseChord(s).chord; }

bool hasPc(const Voicing& v, int pc) {
    return std::any_of(v.begin(), v.end(), [&](int p) { return mod12(p) == mod12(pc); });
}

std::vector<Voicing> lead(const std::vector<std::string>& symbols, VoicingFamily f, int low, int high,
                          const Scale& scale) {
    std::vector<VoicingStep> steps;
    for (const auto& s : symbols) steps.push_back({chord(s), f, true});
    return voiceLead(steps, low, high, scale);
}

const VoicingFamily kAll[] = {VoicingFamily::Close,    VoicingFamily::Open,  VoicingFamily::Drop2,
                              VoicingFamily::Drop3,    VoicingFamily::Rootless, VoicingFamily::Shell,
                              VoicingFamily::Spread,   VoicingFamily::Quartal, VoicingFamily::Power};

}  // namespace

TEST_CASE("every family voices common chords inside the range") {
    const Scale c = makeScale(0, Mode::Major);
    for (const char* s : {"C", "Dm7", "G7", "Cmaj7", "Am9", "G13", "F6", "Bm7b5", "Csus4", "C/E", "E7#9"}) {
        for (auto f : kAll) {
            const std::string symbol = s;
            const std::string family = toString(f);
            CAPTURE(symbol);
            CAPTURE(family);
            auto cands = voicingCandidates(chord(s), f, 48, 79, c);
            REQUIRE_FALSE(cands.empty());
            for (const auto& v : cands) {
                CHECK(v.front() >= 48);
                CHECK(v.back() <= 79);
                CHECK(std::is_sorted(v.begin(), v.end()));
                CHECK(std::adjacent_find(v.begin(), v.end()) == v.end());
                // Every note is a chord tone or a scale tension.
                for (int p : v) CHECK((chord(s).containsPc(p) || c.contains(p)));
            }
        }
    }
}

TEST_CASE("family shapes have their characteristic structure") {
    const Scale c = makeScale(0, Mode::Major);
    // Rootless A/B forms on G13: 3-13-7-9 and 7-9-3-13, no root.
    auto rootless = voicingShapes(chord("G13"), VoicingFamily::Rootless, c);
    REQUIRE(rootless.size() == 2);
    CHECK(rootless[0] == std::vector<int>{4, 9, 10, 14});
    CHECK(rootless[1] == std::vector<int>{10, 14, 16, 21});
    // Shells: 1-3-7 and 1-7-3.
    auto shell = voicingShapes(chord("Dm7"), VoicingFamily::Shell, c);
    CHECK(shell[0] == std::vector<int>{0, 3, 10});
    CHECK(shell[1] == std::vector<int>{0, 10, 15});
    // Drop 2 of root-position Cmaj7 (C E G B) -> G C E B.
    auto drop2 = voicingShapes(chord("Cmaj7"), VoicingFamily::Drop2, c);
    CHECK(std::find(drop2.begin(), drop2.end(), std::vector<int>{-5, 0, 4, 11}) != drop2.end());
    // Quartal stacks are fourths.
    for (const auto& q : voicingShapes(chord("Dm7"), VoicingFamily::Quartal, c))
        for (std::size_t i = 1; i + 1 < q.size(); ++i) CHECK((q[i] - q[i - 1] == 5 || q[i] - q[i - 1] == 6));
    CHECK(voicingShapes(chord("E5"), VoicingFamily::Power, c).front() == std::vector<int>{0, 7});
    // Rootless on a triad is not defined; the candidate search falls back.
    CHECK(voicingShapes(chord("C"), VoicingFamily::Rootless, c).empty());
    bool fallback = false;
    CHECK_FALSE(voicingCandidates(chord("C"), VoicingFamily::Rootless, 48, 79, c, &fallback).empty());
    CHECK(fallback);
}

TEST_CASE("ii-V-I voice-leads with small movement") {
    const Scale c = makeScale(0, Mode::Major);
    for (auto f : {VoicingFamily::Close, VoicingFamily::Drop2, VoicingFamily::Rootless, VoicingFamily::Shell}) {
        const std::string family = toString(f);
        CAPTURE(family);
        auto vs = lead({"Dm7", "G7", "Cmaj7"}, f, 48, 76, c);
        REQUIRE(vs.size() == 3);
        for (std::size_t i = 1; i < vs.size(); ++i) {
            CAPTURE(i);
            // Shells keep the root in the bass, so the root's fourth/fifth motion counts too.
            CHECK(voiceMovement(vs[i - 1], vs[i]) <= (f == VoicingFamily::Shell ? 8 : 6));
            CHECK(std::abs(vs[i].back() - vs[i - 1].back()) <= 3);  // smooth top line
        }
        // Guide tones are always present.
        CHECK(hasPc(vs[0], 5));
        CHECK(hasPc(vs[0], 0));
        CHECK(hasPc(vs[1], 11));
        CHECK(hasPc(vs[1], 5));
        CHECK(hasPc(vs[2], 4));
        CHECK(hasPc(vs[2], 11));
    }
}

TEST_CASE("rootless Dm9 -> G13 alternates A and B forms (one note moves)") {
    const Scale d = makeScale(2, Mode::Dorian);
    auto vs = lead({"Dm9", "G13"}, VoicingFamily::Rootless, 45, 74, d);
    CHECK(vs[0] == Voicing{53, 57, 60, 64});  // F A C E
    CHECK(vs[1] == Voicing{53, 57, 59, 64});  // F A B E
    CHECK(voiceMovement(vs[0], vs[1]) == 1);
}

TEST_CASE("first chord is centred in the range") {
    const Scale c = makeScale(0, Mode::Major);
    auto vs = lead({"C"}, VoicingFamily::Close, 48, 84, c);
    double mean = 0;
    for (int p : vs[0]) mean += p;
    mean /= static_cast<double>(vs[0].size());
    CHECK(std::abs(mean - 66.0) <= 4.0);
}

TEST_CASE("slash bass goes lowest when the range allows") {
    const Scale c = makeScale(0, Mode::Major);
    auto vs = lead({"C/E", "Fm9/Ab"}, VoicingFamily::Close, 40, 76, c);
    CHECK(mod12(vs[0].front()) == 4);
    CHECK(mod12(vs[1].front()) == 8);
    // No room below the voicing: the bass is left to the bass part.
    auto tight = lead({"C/E"}, VoicingFamily::Close, 60, 67, c);
    CHECK(tight[0].front() >= 60);
}

TEST_CASE("voice movement metric") {
    CHECK(voiceMovement({60, 64, 67}, {60, 64, 67}) == 0);
    CHECK(voiceMovement({60, 64, 67}, {59, 65, 67}) == 2);
    CHECK(voiceMovement({60, 64, 67}, {60, 64, 67, 71}) == 2);
}
