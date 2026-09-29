#include <doctest/doctest.h>

#include "flowstate/theory.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace flowstate;

namespace {

std::vector<int> pcsOf(const std::string& symbol) {
    auto parsed = parseChord(symbol);
    REQUIRE_MESSAGE(parsed.chord.has_value(), symbol);
    CHECK_MESSAGE(parsed.warning.empty(), symbol << ": " << parsed.warning);
    return parsed.chord->pitchClasses();
}

std::vector<int> v(std::initializer_list<int> l) {
    std::vector<int> out(l);
    for (auto& x : out) x = mod12(x);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace

TEST_CASE("note names") {
    CHECK(noteNameToMidi("C4") == 60);
    CHECK(noteNameToMidi("A4") == 69);
    CHECK(noteNameToMidi("F#2") == 42);
    CHECK(noteNameToMidi("Bb5") == 82);
    CHECK(noteNameToMidi("C-1") == 0);
    CHECK(noteNameToMidi("G9") == 127);
    CHECK(noteNameToMidi("B#3") == 60);
    CHECK(noteNameToMidi("Cb4") == 59);
    CHECK(noteNameToMidi("H2") == -1);
    CHECK(noteNameToMidi("C") == -1);
    CHECK(noteNameToMidi("G#9") == -1);
    CHECK(midiToNoteName(61) == "C#4");
    CHECK(midiToNoteName(70, true) == "Bb4");
    CHECK(pitchClassFromName("Db") == 1);
    CHECK(pitchClassFromName("E#") == 5);
}

TEST_CASE("scales and degrees") {
    Scale d = makeScale(2, Mode::Dorian);
    CHECK(d.degreeOffset(1) == 0);
    CHECK(d.degreeOffset(3) == 3);
    CHECK(d.degreeOffset(8) == 12);
    CHECK(d.degreeOffset(0) == -2);    // 7th degree an octave down
    CHECK(d.degreeOffset(-6) == -12);  // tonic an octave down
    CHECK(d.degreeOffset(10) == 15);
    CHECK(d.contains(71));  // B natural in D dorian
    CHECK_FALSE(d.contains(70));
    Scale pent = makeScale(9, Mode::MinorPentatonic);
    CHECK(pent.size() == 5);
    CHECK(pent.degreeOffset(6) == 12);
    CHECK(makeScale(0, Mode::Blues).contains(6));
    CHECK(makeScale(0, Mode::HarmonicMinor).contains(11));
    CHECK(d.stepUp(62) == 64);
    CHECK(d.stepDown(62) == 60);
}

TEST_CASE("chord symbol table (every symbol in the spec)") {
    struct Row {
        const char* symbol;
        std::vector<int> pcs;
    };
    const std::vector<Row> rows = {
        {"C", v({0, 4, 7})},
        {"Cm", v({0, 3, 7})},
        {"C7", v({0, 4, 7, 10})},
        {"Cmaj7", v({0, 4, 7, 11})},
        {"Cm7", v({0, 3, 7, 10})},
        {"Cm9", v({0, 3, 7, 10, 14})},
        {"C9", v({0, 4, 7, 10, 14})},
        {"C11", v({0, 5, 7, 10, 14})},
        {"C13", v({0, 4, 7, 10, 14, 21})},
        {"Cm11", v({0, 3, 7, 10, 14, 17})},
        {"Cdim", v({0, 3, 6})},
        {"Cdim7", v({0, 3, 6, 9})},
        {"Cm7b5", v({0, 3, 6, 10})},
        {"Caug", v({0, 4, 8})},
        {"Csus2", v({0, 2, 7})},
        {"Csus4", v({0, 5, 7})},
        {"C7sus4", v({0, 5, 7, 10})},
        {"C6", v({0, 4, 7, 9})},
        {"Cm6", v({0, 3, 7, 9})},
        {"C69", v({0, 4, 7, 9, 14})},
        {"Cadd9", v({0, 4, 7, 14})},
        {"C7b9", v({0, 4, 7, 10, 13})},
        {"C7#9", v({0, 4, 7, 10, 15})},
        {"C7#11", v({0, 4, 7, 10, 18})},
        {"C7b13", v({0, 4, 7, 10, 20})},
        {"C5", v({0, 7})},
        {"C/E", v({0, 4, 7})},
        {"Fm9/Ab", v({5, 8, 12, 15, 19})},
        // Common spellings beyond the spec list.
        {"CM7", v({0, 4, 7, 11})},
        {"C-7", v({0, 3, 7, 10})},
        {"Cmin7", v({0, 3, 7, 10})},
        {"CmMaj7", v({0, 3, 7, 11})},
        {"Cm(maj7)", v({0, 3, 7, 11})},
        {"Co7", v({0, 3, 6, 9})},
        {"C+", v({0, 4, 8})},
        {"C7#5", v({0, 4, 8, 10})},
        {"C6/9", v({0, 4, 7, 9, 14})},
        {"Cmaj9", v({0, 4, 7, 11, 14})},
        {"Cmaj7#11", v({0, 4, 7, 11, 18})},
        {"C9sus4", v({0, 5, 7, 10, 14})},
        {"C13b9", v({0, 4, 7, 10, 13, 21})},
        {"C7(b9,#11)", v({0, 4, 7, 10, 13, 18})},
        {"C7alt", v({0, 4, 10, 13, 15, 20})},
        {"Cm13", v({0, 3, 7, 10, 14, 17, 21})},
        {"Cmadd9", v({0, 3, 7, 14})},
        {"Bb13", v({10, 14, 17, 20, 24, 31})},
        {"F#m7b5", v({6, 9, 12, 16})},
        {"Ebmaj7", v({3, 7, 10, 14})},
    };
    for (const auto& row : rows) {
        const std::string symbol = row.symbol;
        CAPTURE(symbol);
        CHECK(pcsOf(row.symbol) == row.pcs);
    }
}

TEST_CASE("chord structure details") {
    auto c = *parseChord("C/E").chord;
    CHECK(c.bass == 4);
    CHECK(c.bassRoot() == 4);
    auto fm = *parseChord("Fm9/Ab").chord;
    CHECK(fm.root == 5);
    CHECK(fm.bass == 8);
    CHECK(fm.third == 3);
    CHECK(fm.seventh == 10);
    CHECK(fm.hasTension(14));
    auto half = *parseChord("Bm7b5").chord;
    CHECK(half.fifth == 6);
    CHECK(half.seventh == 10);
    CHECK(parseChord("Cdim7").chord->seventh == 9);
    CHECK(parseChord("C6").chord->guideSeventh() == 9);
    auto sus = *parseChord("G7sus4").chord;
    CHECK(sus.sus);
    CHECK(sus.third == 5);
    auto power = *parseChord("E5").chord;
    CHECK(power.power);
    CHECK(power.intervals() == std::vector<int>{0, 7});
    auto a7 = *parseChord("A7#9").chord;
    CHECK(a7.root == 9);
    CHECK(a7.hasTension(15));
    CHECK_FALSE(a7.hasTension(14));
    CHECK(parseChord("C/C").chord->bass == -1);
}

TEST_CASE("chord parser is graceful on bad input") {
    CHECK_FALSE(parseChord("").chord.has_value());
    CHECK_FALSE(parseChord("H7").chord.has_value());
    auto odd = parseChord("C7zz");
    REQUIRE(odd.chord.has_value());
    CHECK_FALSE(odd.warning.empty());
    CHECK(odd.chord->seventh == 10);
}
