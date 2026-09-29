// Groove: deterministic RNG, swing time-warp, energy/accent velocity, humanize.
#pragma once

#include "flowstate/timing.h"

#include <cstdint>
#include <string>

namespace flowstate {

// SplitMix64: tiny, portable, identical on every platform/stdlib.
class Rng {
public:
    explicit Rng(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next();
    // Uniform in [0, 1).
    double uniform();
    // Uniform integer in [0, n).
    int below(int n);
    // Triangular in [-1, 1] (sum of two uniforms), peaks at 0.
    double triangular();

private:
    std::uint64_t state_;
};

std::uint64_t fnv1a(const std::string& s);
std::uint64_t mixSeed(std::uint64_t seed, const std::string& salt);

// Swing as a monotonic time warp inside each pair of steps: the off-beat
// step boundary is delayed by swing * stepLength, everything in between is
// scaled linearly. Only even grids swing (odd 16ths for grid 4, odd 8ths for
// grid 2, ...); triplet grids are already swung.
class Swing {
public:
    Swing(const TimeGrid& time, int grid, double amount);
    Tick apply(Tick t) const;
    bool active() const { return delay_ > 0.0; }

private:
    double pair_ = 0.0;   // ticks in a swing pair (2 steps)
    double step_ = 0.0;
    double delay_ = 0.0;
};

// Velocity factor from section energy (0.6 is neutral).
double energyFactor(double energy);

inline constexpr int kAccentBoost = 18;
inline constexpr double kGhostFactor = 0.4;

int clampVelocity(double v);

// Metric weight of a tick: +1 bar downbeat, +0.5 beat (or pulse in eighth
// meters: 3+3 compound, 2+2+3 odd), 0 off-beat, -0.5 finer subdivisions.
// Used for played dynamics and strong-beat decisions.
double metricWeight(const TimeGrid& time, Tick tick);

}  // namespace flowstate
