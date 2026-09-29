// Music theory primitives: note names, scales/modes, chord symbols.
#pragma once

#include "flowstate/ir.h"

#include <optional>
#include <string>
#include <vector>

namespace flowstate {

// "C", "F#", "Bb" -> 0..11, or -1.
int pitchClassFromName(const std::string& name);
// "C4" = 60, "F#2", "Bb5", "C-1" = 0. Returns -1 if invalid or outside 0..127.
int noteNameToMidi(const std::string& name);
std::string midiToNoteName(int midi, bool preferFlats = false);

inline int mod12(int x) { return ((x % 12) + 12) % 12; }

// A scale: tonic pitch class plus ascending semitone offsets from the tonic
// (first element 0, all < 12). Pentatonic/blues scales have 5/6 degrees.
struct Scale {
    int tonic = 0;
    std::vector<int> intervals;

    bool contains(int pitch) const;          // pitch class membership
    int size() const { return static_cast<int>(intervals.size()); }
    // Degree (1-based, wraps across octaves; 0 = last degree an octave down)
    // -> semitone offset from the tonic.
    int degreeOffset(int degree) const;
    // Nearest scale pitch strictly above/below `pitch` (step motion).
    int stepUp(int pitch) const;
    int stepDown(int pitch) const;
    // Nearest scale pitch to `pitch` (ties go down).
    int snap(int pitch) const;
};

Scale makeScale(int tonicPc, Mode mode);
Scale makeScale(const Context& ctx);

// Parsed chord symbol. All intervals are semitones above the root.
struct Chord {
    std::string symbol;
    int root = 0;          // pitch class
    int bass = -1;         // slash bass pitch class, -1 if none
    int third = 4;         // 3 or 4; 2/5 when sus; -1 when omitted
    bool sus = false;
    int fifth = 7;         // 6, 7, 8, or -1 when omitted
    int seventh = -1;      // 9 (dim7), 10, 11, or -1
    int sixth = -1;        // 9 or -1
    std::vector<int> tensions;  // compound intervals (> 12): 13 b9, 14 9, 15 #9, 17 11, 18 #11, 20 b13, 21 13
    bool power = false;

    // The "seventh slot" used by shells/rootless voicings: seventh, else sixth, else -1.
    int guideSeventh() const { return seventh >= 0 ? seventh : sixth; }
    bool hasTension(int interval) const;
    // All distinct intervals (0..23), sorted, root first.
    std::vector<int> intervals() const;
    // All distinct pitch classes including slash bass.
    std::vector<int> pitchClasses() const;
    bool containsPc(int pc) const;
    // Bass-part root: slash bass when present.
    int bassRoot() const { return bass >= 0 ? bass : root; }
};

struct ChordParse {
    std::optional<Chord> chord;
    std::string warning;  // non-empty when part of the symbol was not understood
};

ChordParse parseChord(const std::string& symbol);

// Diatonic triad (or tetrad if `seventh`) on a scale degree; used when
// harmony is empty.
Chord diatonicChord(const Scale& scale, int degree, bool seventh);

}  // namespace flowstate
