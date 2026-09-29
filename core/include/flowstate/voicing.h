// Chord voicing families and voice-leading search.
#pragma once

#include "flowstate/ir.h"
#include "flowstate/theory.h"

#include <string>
#include <vector>

namespace flowstate {

using Voicing = std::vector<int>;  // ascending MIDI pitches

// The shapes a family offers for a chord, as ascending semitone offsets from
// the chord root (octave placement not yet chosen). Empty when the family
// cannot voice the chord (e.g. rootless on a triad falls back to close).
std::vector<std::vector<int>> voicingShapes(const Chord& chord, VoicingFamily family, const Scale& scale);

// All placements of the family's shapes inside [low, high]. Falls back to
// close voicings, then to folding chord tones into the range, so the result
// is never empty for a non-degenerate range.
std::vector<Voicing> voicingCandidates(const Chord& chord, VoicingFamily family, int low, int high,
                                       const Scale& scale, bool* usedFallback = nullptr);

// Total semitone movement between two voicings (optimal pairing for equal
// sizes, symmetric nearest-note distance otherwise).
int voiceMovement(const Voicing& a, const Voicing& b);

struct VoicingStep {
    Chord chord;
    VoicingFamily family = VoicingFamily::Close;
    bool includeSlashBass = true;  // pads/chords: put a slash bass lowest when the range allows
};

// Chooses one voicing per step minimising movement over the whole sequence
// (Viterbi), with the first chord pulled toward the centre of the range and
// penalties for muddy low intervals and top-voice leaps.
std::vector<Voicing> voiceLead(const std::vector<VoicingStep>& steps, int low, int high, const Scale& scale,
                               std::vector<std::string>* warnings = nullptr);

}  // namespace flowstate
