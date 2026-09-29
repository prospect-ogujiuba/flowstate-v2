// Post-realization constraints: clip, range, monophony, lengths, key report.
#pragma once

#include "flowstate/theory.h"
#include "flowstate/timing.h"

#include <string>
#include <vector>

namespace flowstate {

// Working note used by realizers before it becomes a NoteEvent.
struct RawNote {
    Tick tick = 0;
    Tick dur = 0;
    int pitch = 60;
    int vel = 100;
    bool justified = false;  // chromatic by intent: chord symbol tone, alter, literal, approach
    bool anchor = false;     // downbeat kick/snare: tighter humanize
    bool fixedPitch = false; // literal drum notes etc.: never range-folded
    std::string sublane;
};

inline constexpr Tick kMinNoteTicks = kPpq / 16;  // 1/64 note

// Stable order: tick, pitch, then original order.
void sortNotes(std::vector<RawNote>& notes);

// Drops onsets outside [0, clipEnd), clamps note-offs to clipEnd, drops
// non-positive durations.
void clipToClip(std::vector<RawNote>& notes, Tick clipEnd);

// Folds pitches into [low, high] by octaves. Returns how many notes could not
// fit (range narrower than the pitch class spacing); those are clamped.
int foldRange(std::vector<RawNote>& notes, int low, int high);
int foldPitch(int pitch, int low, int high);

// Removes same-pitch same-onset duplicates (keeps the loudest, longest) and
// truncates a note at the next onset of the same pitch.
void dedupe(std::vector<RawNote>& notes);

// One note at a time: keeps the highest (or lowest) of simultaneous onsets
// and truncates each note at the next onset.
void enforceMonophony(std::vector<RawNote>& notes, bool keepLowest);

// Extends notes shorter than kMinNoteTicks where there is room (next onset
// for monophonic parts, clip end); drops anything still zero-length.
void enforceMinLength(std::vector<RawNote>& notes, Tick clipEnd, bool monophonic);

// Pitches outside the scale that no chord symbol/alter/literal justifies.
std::vector<const RawNote*> findOutOfKey(const std::vector<RawNote>& notes, const Scale& scale);

}  // namespace flowstate
