#include "flowstate/timing.h"

#include <cmath>

namespace flowstate {

Tick roundHalfUp(double x) { return static_cast<Tick>(std::floor(x + 0.5)); }

TimeGrid::TimeGrid(int meterNumerator, int meterDenominator, int bars, double tempo)
    : num_(meterNumerator), den_(meterDenominator), bars_(bars), tempo_(tempo) {
    // ticksPerBar = numerator * 960 * 4 / denominator; the denominator is a
    // power of two <= 32, so this is exact.
    ticksPerBeat_ = static_cast<Tick>(kPpq) * 4 / den_;
    ticksPerBar_ = static_cast<Tick>(num_) * kPpq * 4 / den_;
}

TimeGrid::TimeGrid(const Context& ctx)
    : TimeGrid(ctx.meterNumerator, ctx.meterDenominator, ctx.bars, ctx.tempo) {}

Tick TimeGrid::at(int bar, double beat) const {
    return barStart(bar) + roundHalfUp((beat - 1.0) * static_cast<double>(ticksPerBeat_));
}

Tick TimeGrid::stepOffset(int index, int grid) const {
    // round(index * ticksPerBeat / grid) with integer half-up rounding.
    Tick numer = static_cast<Tick>(index) * ticksPerBeat_ * 2 + grid;
    return numer / (2 * static_cast<Tick>(grid));
}

}  // namespace flowstate
