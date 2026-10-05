// Shared context for the per-role realizers (internal).
#pragma once

#include "flowstate/constraints.h"
#include "flowstate/groove.h"
#include "flowstate/harmony.h"
#include "flowstate/ir.h"
#include "flowstate/steps.h"
#include "flowstate/theory.h"
#include "flowstate/timing.h"

#include <set>
#include <string>
#include <vector>

namespace flowstate::detail {

// ---- Density (P1-21) ---------------------------------------------------------------------------
// How a pattern thins and fills: which token an added onset gets and where it prefers to go.
enum class DensityKind {
    Chords,       // chords, pad, arp: 'x'; a removed hit extends the previous one
    Bass,         // 'x' on stronger steps, ghost roots 'g' on weak ones
    Line,         // melody and counter rhythms: 'x', the next step of the line
    DrumTime,     // hats, ride, shaker, tambourine: 'x', strongest empty steps first
    DrumGhost,    // kick, snare, clap, rim, cowbell: ghost notes on off-beats
    DrumFixed,    // crash, ride bell, toms: thinned, never filled
};
DensityKind drumDensityKind(DrumVoice voice);

// Thins (density < 0.5) or fills (density > 0.5) a step pattern; 0.5 leaves it untouched. Thinning
// removes ghosts first, then the weakest steps, and keeps the strongest onset of every bar; at 0
// only that onset is left. Filling adds up to as many onsets as the pattern has (at 1), on the
// steps the kind prefers. `grid` is the pattern's steps per beat before its scale. Ties are broken
// by `seed`, so the same pattern, density and seed always give the same result.
void applyDensity(StepPattern& pattern, double density, DensityKind kind, const TimeGrid& time, int grid,
                  std::uint64_t seed);

// The same for a motif's notes (after its transforms): thinning merges the weakest short notes
// into the note before them, filling splits the longest notes, the second half a scale step
// toward the next note.
std::vector<MotifNote> applyDensity(std::vector<MotifNote> notes, double density, const TimeGrid& time,
                                    std::uint64_t seed);

// ---- Re-roll variants (P1-22) -------------------------------------------------------------------
// For a part with its own seed: the pattern written out over the block's bars, each bar with a
// chance of one small move (a push, a passing tone, a ghost note or a dropped hat).
void varyPattern(StepPattern& pattern, int blockBars, DensityKind kind, const TimeGrid& time, int grid, Rng& rng);
// A motif repeat with a chance of one short note moved a scale step.
std::vector<MotifNote> varyMotif(std::vector<MotifNote> notes, Rng& rng);

struct BlockSpan {
    int index = 0;    // position in part.blocks
    int startBar = 1; // clipped to the clip
    int endBar = 1;   // inclusive, clipped
    Tick start = 0;
    Tick end = 0;     // exclusive
    int bars() const { return endBar - startBar + 1; }
};

class PartEnv {
public:
    PartEnv(const Score& score, const Part& part, const TimeGrid& time, const Harmony& harmony,
            const Scale& scale, std::uint64_t seed, std::vector<std::string>& warnings, double density = 0.5,
            bool variant = false);

    const Score& score;
    const Part& part;
    const TimeGrid& time;
    const Harmony& harmony;
    const Scale& scale;
    const int low;
    const int high;
    const Swing swing;
    const std::uint64_t seed;
    std::vector<std::string>& warnings;
    // The part's effective density: its written `density` plus the live knob (0.5 = as written).
    const double density;
    // The part has its own seed (re-roll): it makes seeded choices. Without one it plays as written.
    const bool variant;

    // A re-rolled part's take on a pattern of block `b` (a no-op without a part seed).
    void vary(StepPattern& pat, DensityKind kind, const BlockSpan& b, const std::string& label) const;
    // A generator for a re-rolled part's other choices, independent of every other draw.
    Rng variantRng(const std::string& label) const { return Rng(mixSeed(seed, "variant:" + label)); }

    // Applies the part's density to a pattern of this block (a no-op at 0.5). `label` names the
    // pattern (a lane), so each one breaks its ties differently.
    void thin(StepPattern& pat, DensityKind kind, const std::string& label) const;

    // Valid blocks in IR order, clipped to the clip.
    const std::vector<BlockSpan>& blocks() const { return blocks_; }
    const Block& block(const BlockSpan& b) const { return part.blocks[static_cast<std::size_t>(b.index)]; }
    // True when `blockIndex` owns the bar containing `t` (later blocks win overlaps).
    bool owns(int blockIndex, Tick t) const;

    double energyAt(Tick t) const;
    // Base velocity scaled by energy, plus accent / ghost / metric shaping.
    int velocity(Tick t, bool accent, bool ghost, double metricScale) const;

    // Absolute tick of a block-relative global step (before swing), at the part's grid times `scale`
    // (a pattern read finer than the grid, see StepPattern::scale).
    Tick stepTick(const BlockSpan& b, int globalStep, int scale = 1) const;
    Tick stepTick(const BlockSpan& b, int globalStep, const StepPattern& pat) const;
    // Swung tick of a block-relative global step.
    Tick swungStep(const BlockSpan& b, int globalStep, const StepPattern& pat) const {
        return swing.apply(stepTick(b, globalStep, pat));
    }
    double stepTicks() const { return time.stepTicks(part.grid); }

    StepPattern pattern(const BlockSpan& b, const std::string& text, const std::string& allowed,
                        const std::string& label) const;
    std::string where(const BlockSpan& b) const;
    void warnOnce(const std::string& msg) const;

    const Motif* findMotif(const std::string& id) const;
    // Tonic pitch nearest the middle of the register (motif degree reference).
    int registerCentre() const;

    Articulation articulation(const BlockSpan& b, Articulation fallback) const;

private:
    std::vector<BlockSpan> blocks_;
    std::vector<int> barOwner_;  // 1-based bars
    mutable std::set<std::string> warned_;
};

// Gate fraction of the step span per articulation (legato handled by callers).
double gateFor(Articulation a);


std::vector<RawNote> realizeChordPart(const PartEnv& env);  // chords + pad
std::vector<RawNote> realizeArpPart(const PartEnv& env);
std::vector<RawNote> realizeBassPart(const PartEnv& env);
std::vector<RawNote> realizeMelodyPart(const PartEnv& env);  // melody + counter
std::vector<RawNote> realizeDrumPart(const PartEnv& env);

// Literal `notes` of every block (any role).
std::vector<RawNote> realizeLiteralNotes(const PartEnv& env);

}  // namespace flowstate::detail
