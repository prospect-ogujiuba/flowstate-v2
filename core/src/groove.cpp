#include "flowstate/groove.h"

#include <algorithm>
#include <cmath>

namespace flowstate {

std::uint64_t Rng::next() {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double Rng::uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

int Rng::below(int n) {
    if (n <= 1) return 0;
    return static_cast<int>(next() % static_cast<std::uint64_t>(n));
}

double Rng::triangular() { return uniform() + uniform() - 1.0; }

std::uint64_t fnv1a(const std::string& s) {
    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ULL;
    }
    return h;
}

std::uint64_t mixSeed(std::uint64_t seed, const std::string& salt) {
    Rng r(seed ^ fnv1a(salt));
    return r.next();
}

Swing::Swing(const TimeGrid& time, int grid, double amount) {
    if (grid < 2 || grid % 2 != 0 || amount <= 0.0) return;
    step_ = static_cast<double>(time.ticksPerBeat()) / grid;
    pair_ = step_ * 2.0;
    delay_ = std::clamp(amount, 0.0, 0.75) * step_;
}

Tick Swing::apply(Tick t) const {
    if (delay_ <= 0.0 || t <= 0) return t;
    double td = static_cast<double>(t);
    double pairIndex = std::floor(td / pair_);
    double o = td - pairIndex * pair_;
    double warped;
    if (o <= step_) warped = o * (step_ + delay_) / step_;
    else warped = step_ + delay_ + (o - step_) * (step_ - delay_) / step_;
    return roundHalfUp(pairIndex * pair_ + warped);
}

double energyFactor(double energy) {
    return std::clamp(1.0 + (energy - 0.6) * 0.5, 0.6, 1.25);
}

int clampVelocity(double v) { return static_cast<int>(std::clamp(std::lround(v), 1L, 127L)); }

double metricWeight(const TimeGrid& time, Tick tick) {
    const Tick bar = time.ticksPerBar();
    const Tick beat = time.ticksPerBeat();
    Tick inBar = ((tick % bar) + bar) % bar;
    if (inBar == 0) return 1.0;
    if (time.denominator() >= 8 && time.numerator() > 3) {
        // Eighth-note meters group beats: 3+3 in compound meters (6/8, 9/8,
        // 12/8), otherwise 2+2+...+3 (5/8, 7/8, 11/8). Group starts are the
        // pulse; the other beats are weak.
        if (inBar % beat != 0) return -0.5;
        const Tick idx = inBar / beat;
        const int num = time.numerator();
        bool pulse;
        if (num % 3 == 0) pulse = idx % 3 == 0;
        else pulse = (idx % 2 == 0 && idx <= num - 3);
        return pulse ? 0.5 : 0.0;
    }
    if (inBar % beat == 0) return 0.5;
    if ((inBar * 2) % beat == 0) return 0.0;
    return -0.5;
}

}  // namespace flowstate
