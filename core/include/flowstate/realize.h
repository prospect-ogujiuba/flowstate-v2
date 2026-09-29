// Realization: score IR -> deterministic notes per part.
#pragma once

#include "flowstate/ir.h"
#include "flowstate/timing.h"

#include <cstdint>
#include <string>
#include <vector>

namespace flowstate {

struct NoteEvent {
    Tick tick = 0;
    Tick dur = 0;
    int pitch = 60;
    int vel = 100;
    std::string sublane;  // drums only (v1 sublane id), empty otherwise
};

struct PartRealization {
    std::string id;
    Role role = Role::Chords;
    std::string name;
    int channel = 0;  // 0-based MIDI channel (drums = 9)
    int low = 0;
    int high = 127;
    std::vector<NoteEvent> notes;  // sorted by tick, then pitch
};

struct OutOfKeyNote {
    std::string part;
    Tick tick = 0;
    int pitch = 0;
};

struct Realization {
    std::string title;
    int ppq = kPpq;
    int bars = 0;
    Tick ticksPerBar = 0;
    double tempo = 120.0;
    int meterNumerator = 4;
    int meterDenominator = 4;
    std::vector<PartRealization> parts;
    std::vector<std::string> warnings;
    std::vector<OutOfKeyNote> outOfKey;

    Tick clipEnd() const { return ticksPerBar * bars; }
};

struct RealizeOptions {
    std::uint64_t seed = 1;
    bool humanize = true;
};

Realization realize(const Score& score, const RealizeOptions& options = {});

// Parse + validate + realize; parse warnings come first in `warnings`.
// Throws IrError on invalid input.
Realization realizeJson(const std::string& jsonText, const RealizeOptions& options = {});

// v1 drum sublane for a GM drum note ("kick", "snare", "clap_rim", "hats",
// "toms", "cymbals", "aux_kit").
const char* drumSublaneForNote(int gmNote);
int drumVoiceNote(DrumVoice voice);

}  // namespace flowstate
