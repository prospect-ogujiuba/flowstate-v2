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
            const Scale& scale, std::uint64_t seed, std::vector<std::string>& warnings);

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

    // Valid blocks in IR order, clipped to the clip.
    const std::vector<BlockSpan>& blocks() const { return blocks_; }
    const Block& block(const BlockSpan& b) const { return part.blocks[static_cast<std::size_t>(b.index)]; }
    // True when `blockIndex` owns the bar containing `t` (later blocks win overlaps).
    bool owns(int blockIndex, Tick t) const;

    double energyAt(Tick t) const;
    // Base velocity scaled by energy, plus accent / ghost / metric shaping.
    int velocity(Tick t, bool accent, bool ghost, double metricScale) const;

    // Absolute tick of a block-relative global step (before swing).
    Tick stepTick(const BlockSpan& b, int globalStep) const;
    // Swung tick of a block-relative global step.
    Tick swungStep(const BlockSpan& b, int globalStep) const { return swing.apply(stepTick(b, globalStep)); }
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
