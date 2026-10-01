// Score IR v0 types, JSON parsing and validation. Semantics: docs/ir-spec.md.
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace flowstate {

inline constexpr const char* kIrId = "flowstate.score.v0";

enum class Mode {
    Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian,
    HarmonicMinor, MelodicMinor, MajorPentatonic, MinorPentatonic, Blues
};

enum class Role { Chords, Pad, Arp, Bass, Melody, Counter, Drums };

enum class VoicingFamily { Close, Open, Drop2, Drop3, Rootless, Shell, Spread, Quartal, Power };

enum class ArpPattern { Up, Down, UpDown, Random, ChordTones };

enum class Articulation { Legato, Normal, Staccato };

enum class DrumVoice {
    Kick, Snare, Clap, Rim, ClosedHat, PedalHat, OpenHat, LowTom, MidTom, HighTom,
    Crash, Ride, RideBell, Shaker, Tambourine, Cowbell
};

enum class Fill { None, SnareRoll, TomRun, KickBuild, CrashEnd, HalfTimeBreak };

struct Context {
    double tempo = 120.0;
    int meterNumerator = 4;
    int meterDenominator = 4;
    std::string tonic = "C";
    Mode mode = Mode::Major;
    int bars = 4;
    double swing = 0.0;
    std::vector<std::string> style;
};

struct Section {
    std::string name;
    int startBar = 1;
    int bars = 1;
    double energy = 0.6;
};

struct ChordEntry {
    int bar = 1;
    double beat = 1.0;
    double beats = 4.0;
    std::string symbol;
};

struct MotifNote {
    int degree = 1;
    int octave = 0;
    int alter = 0;
    double beat = 0.0;
    double beats = 1.0;
    bool accent = false;
};

struct Motif {
    std::string id;
    std::vector<MotifNote> notes;
};

struct DrumLane {
    DrumVoice voice = DrumVoice::Kick;
    std::string steps;
};

struct LiteralNote {
    int bar = 1;
    double beat = 1.0;
    double beats = 1.0;
    std::string pitch;
    int velocity = 100;
};

struct Block {
    int startBar = 1;
    int endBar = 1;  // inclusive
    std::optional<std::string> rhythm;
    std::optional<VoicingFamily> voicing;
    std::optional<ArpPattern> arpPattern;
    std::optional<std::string> motif;
    std::optional<std::vector<std::string>> transforms;
    std::optional<double> repeatEvery;
    std::optional<std::vector<DrumLane>> drums;
    std::optional<std::string> groove;  // drums: a named groove (grooves.h); `drums` lanes replace its voices
    std::optional<Fill> fill;
    std::optional<std::vector<LiteralNote>> notes;
    std::optional<Articulation> articulation;
};

struct Part {
    std::string id;
    Role role = Role::Chords;
    std::string name;
    std::string low = "C3";
    std::string high = "C5";
    int grid = 4;
    int velocity = 90;
    std::vector<Block> blocks;
};

struct Score {
    std::string ir = kIrId;
    std::string title;
    Context context;
    std::vector<Section> form;
    std::vector<ChordEntry> harmony;
    std::vector<Motif> motifs;
    std::vector<Part> parts;
};

// Thrown for input that cannot be realized at all (invalid JSON, wrong ir id,
// missing required structure, unknown enum values in required fields).
class IrError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Parses and validates. Recoverable problems are clamped/defaulted and
// appended to `warnings`; unrecoverable ones throw IrError.
Score parseScore(const std::string& jsonText, std::vector<std::string>& warnings);

// Validation that does not change the score (tiling, overlaps, ranges).
void validateScore(const Score& score, std::vector<std::string>& warnings);

const char* toString(Role role);
const char* toString(DrumVoice voice);
const char* toString(VoicingFamily family);
std::optional<Role> roleFromString(const std::string& s);
std::optional<Mode> modeFromString(const std::string& s);
std::optional<VoicingFamily> voicingFromString(const std::string& s);
std::optional<ArpPattern> arpPatternFromString(const std::string& s);
std::optional<Articulation> articulationFromString(const std::string& s);
std::optional<DrumVoice> drumVoiceFromString(const std::string& s);
std::optional<Fill> fillFromString(const std::string& s);

bool isPitchedMonophonic(Role role);

}  // namespace flowstate
