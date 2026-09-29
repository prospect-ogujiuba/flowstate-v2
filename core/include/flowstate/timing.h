// Time model: 960 PPQ, beat = meter denominator unit, half-open clip.
#pragma once

#include "flowstate/ir.h"

#include <cstdint>

namespace flowstate {

using Tick = std::int64_t;

inline constexpr int kPpq = 960;

// floor(x + 0.5) for tick conversion (v1 roundHalfUp).
Tick roundHalfUp(double x);

class TimeGrid {
public:
    TimeGrid(int meterNumerator, int meterDenominator, int bars, double tempo);
    explicit TimeGrid(const Context& ctx);

    int numerator() const { return num_; }
    int denominator() const { return den_; }
    int bars() const { return bars_; }
    double tempo() const { return tempo_; }

    Tick ticksPerBeat() const { return ticksPerBeat_; }  // denominator unit
    Tick ticksPerBar() const { return ticksPerBar_; }
    Tick clipEnd() const { return ticksPerBar_ * bars_; }

    // bar is 1-based.
    Tick barStart(int bar) const { return static_cast<Tick>(bar - 1) * ticksPerBar_; }
    // 1-based bar and beat (fractional beats allowed).
    Tick at(int bar, double beat) const;
    // Duration of `beats` denominator beats.
    Tick beatsToTicks(double beats) const { return roundHalfUp(beats * static_cast<double>(ticksPerBeat_)); }

    // Step grid: `grid` steps per beat.
    int stepsPerBar(int grid) const { return num_ * grid; }
    // Tick offset of step `index` within a bar (exact rational rounding).
    Tick stepOffset(int index, int grid) const;
    double stepTicks(int grid) const { return static_cast<double>(ticksPerBeat_) / grid; }

    // Milliseconds to ticks at the score tempo.
    double msToTicks(double ms) const { return ms / 1000.0 * tempo_ / 60.0 * kPpq; }

    bool contains(Tick t) const { return t >= 0 && t < clipEnd(); }

private:
    int num_;
    int den_;
    int bars_;
    double tempo_;
    Tick ticksPerBeat_;
    Tick ticksPerBar_;
};

}  // namespace flowstate
