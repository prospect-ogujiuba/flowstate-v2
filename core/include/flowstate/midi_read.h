// Standard MIDI File reader: SMF type 0 or 1 -> notes at 960 PPQ. Input for the analyzer.
#pragma once

#include "flowstate/timing.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flowstate {

// Limits that keep a hostile or broken file from costing much (the same as v1's profiler).
inline constexpr std::size_t kMaxMidiBytes = 8u * 1024u * 1024u;
inline constexpr int kMaxMidiTracks = 64;
inline constexpr int kMaxMidiNotes = 4096;

struct MidiNote {
    Tick tick = 0;  // at kPpq
    Tick dur = 0;
    int pitch = 60;
    int vel = 100;
    int channel = 0;  // 0-based (drums = 9)
    int track = 0;
};

struct MidiFileData {
    int sourcePpq = 0;           // the file's own resolution
    std::optional<double> tempo;  // first tempo event, BPM
    std::optional<int> meterNumerator;
    std::optional<int> meterDenominator;
    std::vector<std::string> trackNames;
    std::vector<MidiNote> notes;  // sorted by tick, then pitch
    Tick endTick = 0;             // latest end-of-track or note end, at kPpq
    std::vector<std::string> warnings;
};

enum class MidiReadErrorCode { Empty, TooLarge, Malformed, UnsupportedTiming, TooManyTracks, TooManyNotes, NoNotes };

struct MidiReadError {
    MidiReadErrorCode code = MidiReadErrorCode::Malformed;
    std::string message;
};

const char* toString(MidiReadErrorCode code);

// Reads the whole file. Note-on with velocity 0 is a note-off; an unmatched note-on ends at the
// track end, with a warning. SMPTE timing is rejected. Ticks are rescaled to kPpq.
struct MidiReadResult {
    std::optional<MidiFileData> data;
    std::optional<MidiReadError> error;
};
MidiReadResult readSmf(const std::vector<std::uint8_t>& bytes);

}  // namespace flowstate
