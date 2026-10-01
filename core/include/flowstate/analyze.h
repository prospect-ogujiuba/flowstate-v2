// MIDI -> score IR analyzer: profiles a clip, classifies its lane, detects key and grid, writes it as
// IR, and measures how faithfully that IR plays back the original. Semantics: docs/library.md.
#pragma once

#include "flowstate/ir.h"
#include "flowstate/midi_read.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flowstate {

// Lanes a clip can be classified into (v1's four MIDI lanes).
inline constexpr Role kAnalyzedRoles[] = {Role::Drums, Role::Bass, Role::Chords, Role::Melody};

// Bass-safe pitch window (v1's lane rule): every note of a bass clip is inside it.
inline constexpr int kBassLowestPitch = 24;
inline constexpr int kBassHighestPitch = 60;

// Lane classifier thresholds (v1's golden values).
inline constexpr int kLanePrimaryScore = 60;
inline constexpr int kLanePrimaryMargin = 15;
inline constexpr int kLaneHighConfidence = 80;

struct MidiProfile {
    int noteCount = 0;
    int onsetCount = 0;
    int minPitch = 0;
    int maxPitch = 0;
    double medianPitch = 0.0;
    double channel10Ratio = 0.0;     // notes on MIDI channel 10
    double drumPitchRatio = 0.0;     // channel-10 notes on a supported GM drum pitch
    double lowRegisterRatio = 0.0;   // notes in the bass window
    double monoOnsetRatio = 0.0;     // onsets with one note
    double polyOnsetRatio = 0.0;     // onsets with two or more
    double monoActivity = 0.0;       // share of sounding time with one note
    double polyActivity = 0.0;       // share of sounding time with two or more
    double notesPerOnset = 0.0;
    double meanVelocity = 0.0;
    int distinctPitchClasses = 0;
};

struct LaneScore {
    Role role = Role::Chords;
    int score = 0;  // 0..100
    std::string evidence;
};

struct LaneResult {
    std::vector<LaneScore> scores;  // highest first
    std::optional<Role> primary;    // set when the top score is clear (score and margin thresholds)
    bool highConfidence = false;
};

struct KeyEstimate {
    bool reliable = false;
    int tonic = 0;  // pitch class of the best guess
    Mode mode = Mode::Major;
    double correlation = 0.0;
    double margin = 0.0;  // best minus runner-up correlation
    std::string reason;   // why it is (or isn't) reliable; shown when the key is blank
};

struct GridEstimate {
    int grid = 4;         // steps per beat
    double swing = 0.0;   // IR swing (delay of off-beat steps, in steps)
    double fit = 0.0;     // share of onsets within tolerance of the grid
};

struct Fidelity {
    double rhythm = 0.0;  // onset-time F1 between the IR's realization and the original
    double pitch = 0.0;   // mean pitch-class Jaccard of matched onsets
    double notes = 0.0;   // note F1 (onset and exact pitch; pitch class for chords)
    int literalNotes = 0; // original notes the IR keeps as literal notes
};

struct Descriptors {
    double density = 0.0;     // 0..1: onsets per beat, saturating (1 per beat = 0.5)
    double complexity = 0.0;  // 0..1: syncopation, harmonic colour, rhythmic variety
    double energy = 0.0;      // 0..1: velocity and density
    double syncopation = 0.0; // share of onsets off the beat
    std::string groove;       // "straight", "swing" or "triplet"
};

struct AnalyzeOptions {
    std::optional<Role> declaredRole;  // the manifest's lane; content still decides conflicts
    std::string title = "Library clip";
    std::string partName;              // track name in the normalized MIDI and the IR part name
    std::vector<std::string> style;
    // A key the producer knows (the manifest's): it wins over detection and the IR is written in it.
    std::optional<int> keyTonic;  // pitch class
    std::optional<Mode> keyMode;
};

struct Analysis {
    bool ok = false;
    std::string errorCode;  // set when !ok: a MidiReadErrorCode name, or lane_conflict, bass_range, not_drums
    std::string error;      // precise and safe to show (no paths)

    MidiProfile profile;
    LaneResult lanes;
    Role role = Role::Chords;  // the lane the IR is written for
    KeyEstimate key;
    GridEstimate grid;
    Descriptors descriptors;
    double tempo = 120.0;
    bool tempoFromFile = false;
    int meterNumerator = 4;
    int meterDenominator = 4;
    int bars = 0;
    std::vector<std::string> harmony;  // chord symbols in order (chords lane; inferred roots for bass)

    std::string scoreJson;  // flowstate.score.v0
    Fidelity fidelity;
    std::vector<std::uint8_t> normalizedMidi;  // SMF type 1 at 960 PPQ, neutral track names, exact clip length
    std::vector<std::string> warnings;
};

Analysis analyzeMidi(const std::vector<std::uint8_t>& bytes, const AnalyzeOptions& options = {});

// Content profile and lane scores, exposed for tests.
MidiProfile profileNotes(const std::vector<MidiNote>& notes);
LaneResult classifyLane(const MidiProfile& profile);
// Krumhansl-Kessler key estimate over duration-weighted pitch classes (bass notes weigh more).
KeyEstimate estimateKey(const std::vector<MidiNote>& notes);
// Best chord symbol for a set of pitches (lowest pitch = bass); `flats` picks the spelling.
std::string nameChord(const std::vector<int>& pitches, bool flats);

// {"ok","error"?,"role","lanes":[...],"key":{...},"grid":{...},"descriptors":{...},"profile":{...},
//  "tempo","meter":[n,d],"bars","harmony","fidelity":{...},"score":{IR},"warnings":[...]}
std::string analysisJson(const Analysis& a, int indent = 2);

}  // namespace flowstate
