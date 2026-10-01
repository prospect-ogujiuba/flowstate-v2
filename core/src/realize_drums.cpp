// Drums: per-voice step lanes, fills, groove dynamics, GM mapping.
#include "flowstate/realize.h"
#include "realize_internal.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace flowstate::detail {

namespace {

struct DrumHit {
    Tick raw = 0;  // unswung tick
    DrumVoice voice = DrumVoice::Kick;
    char kind = 'x';     // x, X, g
    double scale = 1.0;  // fill crescendo
};

bool isHat(DrumVoice v) {
    return v == DrumVoice::ClosedHat || v == DrumVoice::PedalHat || v == DrumVoice::OpenHat;
}
bool isTom(DrumVoice v) { return v == DrumVoice::LowTom || v == DrumVoice::MidTom || v == DrumVoice::HighTom; }
bool isTimekeeper(DrumVoice v) {
    return isHat(v) || v == DrumVoice::Ride || v == DrumVoice::Shaker || v == DrumVoice::Tambourine;
}

double voiceLevel(DrumVoice v) {
    switch (v) {
        case DrumVoice::Kick: return 1.0;
        case DrumVoice::Snare: return 1.0;
        case DrumVoice::Clap: return 0.95;
        case DrumVoice::Rim: return 0.85;
        case DrumVoice::ClosedHat: return 0.8;
        case DrumVoice::PedalHat: return 0.7;
        case DrumVoice::OpenHat: return 0.85;
        case DrumVoice::LowTom:
        case DrumVoice::MidTom:
        case DrumVoice::HighTom: return 0.95;
        case DrumVoice::Crash: return 0.95;
        case DrumVoice::Ride: return 0.8;
        case DrumVoice::RideBell: return 0.85;
        case DrumVoice::Shaker: return 0.7;
        case DrumVoice::Tambourine: return 0.75;
        case DrumVoice::Cowbell: return 0.8;
    }
    return 1.0;
}

class FillBuilder {
public:
    FillBuilder(const PartEnv& env, std::vector<DrumHit>& hits, Tick barStart)
        : env_(env), hits_(hits), barStart_(barStart), barEnd_(barStart + env.time.ticksPerBar()) {
        const int num = env.time.numerator();
        const int fillBeats = std::max(1, num / 2);
        fillStart_ = barEnd_ - fillBeats * env.time.ticksPerBeat();
        grid_ = std::max(2, env.part.grid);
    }

    void remove(Tick from, Tick to, std::initializer_list<DrumVoice> voices) {
        auto matches = [&](const DrumHit& h) {
            if (h.raw < from || h.raw >= to) return false;
            for (auto v : voices)
                if (h.voice == v) return true;
            return false;
        };
        hits_.erase(std::remove_if(hits_.begin(), hits_.end(), matches), hits_.end());
    }

    void add(Tick t, DrumVoice v, char kind, double scale = 1.0) { hits_.push_back({t, v, kind, scale}); }

    // Fill-grid positions in [from, to) at `perBeat` steps per beat.
    std::vector<Tick> positions(Tick from, Tick to, int perBeat) const {
        std::vector<Tick> out;
        const double step = static_cast<double>(env_.time.ticksPerBeat()) / perBeat;
        for (int k = 0;; ++k) {
            Tick t = from + roundHalfUp(k * step);
            if (t >= to) break;
            out.push_back(t);
        }
        return out;
    }

    void apply(Fill fill) {
        using V = DrumVoice;
        const Tick beat = env_.time.ticksPerBeat();
        switch (fill) {
            case Fill::None: break;
            case Fill::SnareRoll: {
                remove(fillStart_, barEnd_, {V::Snare, V::Clap, V::Rim, V::ClosedHat, V::PedalHat, V::OpenHat,
                                             V::LowTom, V::MidTom, V::HighTom});
                std::vector<Tick> pos = positions(fillStart_, barEnd_ - beat, grid_);
                // Last beat doubles up when the resolution allows (>= 1/64 note).
                const int lastGrid = beat / (2 * grid_) >= kMinNoteTicks ? grid_ * 2 : grid_;
                for (Tick t : positions(barEnd_ - beat, barEnd_, lastGrid)) pos.push_back(t);
                if (fillStart_ == barEnd_ - beat) pos = positions(fillStart_, barEnd_, lastGrid);
                ramp(pos, V::Snare, 0.45, 1.0);
                break;
            }
            case Fill::TomRun: {
                remove(fillStart_, barEnd_, {V::Snare, V::Clap, V::Rim, V::ClosedHat, V::PedalHat, V::OpenHat,
                                             V::LowTom, V::MidTom, V::HighTom});
                auto pos = positions(fillStart_, barEnd_, grid_);
                const std::size_t n = pos.size();
                for (std::size_t i = 0; i < n; ++i) {
                    double f = n > 1 ? static_cast<double>(i) / static_cast<double>(n - 1) : 1.0;
                    V v = i * 3 < n ? V::HighTom : i * 3 < 2 * n ? V::MidTom : V::LowTom;
                    add(pos[i], v, i + 1 == n ? 'X' : 'x', 0.75 + 0.25 * f);
                }
                if (!pos.empty()) add(pos.back(), V::Kick, 'X');
                break;
            }
            case Fill::KickBuild: {
                remove(fillStart_, barEnd_, {V::Kick});
                const Tick mid = fillStart_ + (barEnd_ - fillStart_) / 2;
                std::vector<Tick> pos = positions(fillStart_, mid, std::max(1, grid_ / 2));
                for (Tick t : positions(mid, barEnd_, grid_)) pos.push_back(t);
                ramp(pos, V::Kick, 0.6, 1.0);
                if (!pos.empty()) add(pos.back(), V::Snare, 'X');
                break;
            }
            case Fill::CrashEnd: {
                remove(barStart_, barEnd_, {V::ClosedHat, V::PedalHat, V::OpenHat, V::Ride, V::RideBell});
                remove(barStart_, barStart_ + 1, {V::Kick});
                add(barStart_, V::Crash, 'X');
                add(barStart_, V::Kick, 'X');
                // Snare pickup into whatever comes next.
                auto pos = positions(barEnd_ - beat, barEnd_, grid_);
                if (pos.size() >= 2) {
                    remove(pos[pos.size() - 2], barEnd_, {V::Snare});
                    add(pos[pos.size() - 2], V::Snare, 'x', 0.8);
                    add(pos.back(), V::Snare, 'X');
                }
                break;
            }
            case Fill::HalfTimeBreak: {
                remove(barStart_, barEnd_, {V::Kick, V::Snare, V::Clap, V::Rim, V::ClosedHat, V::PedalHat,
                                            V::OpenHat});
                const int num = env_.time.numerator();
                const Tick half = barStart_ + std::max(1, num / 2) * beat;
                add(barStart_, V::Kick, 'X');
                add(half, V::Snare, 'X');
                Tick late = half + beat + beat / 2;
                if (late < barEnd_) add(late, V::Kick, 'x', 0.85);
                for (Tick t = barStart_; t < barEnd_; t += beat) add(t, V::ClosedHat, t == barStart_ ? 'X' : 'x');
                add(barEnd_ - beat / 2, V::OpenHat, 'x', 0.9);
                break;
            }
        }
    }

private:
    void ramp(const std::vector<Tick>& pos, DrumVoice v, double from, double to) {
        const std::size_t n = pos.size();
        for (std::size_t i = 0; i < n; ++i) {
            double f = n > 1 ? static_cast<double>(i) / static_cast<double>(n - 1) : 1.0;
            add(pos[i], v, i + 1 == n ? 'X' : 'x', from + (to - from) * f);
        }
    }

    const PartEnv& env_;
    std::vector<DrumHit>& hits_;
    Tick barStart_, barEnd_, fillStart_;
    int grid_;
};

}  // namespace

std::vector<RawNote> realizeDrumPart(const PartEnv& env) {
    std::vector<RawNote> out;
    const double step = env.stepTicks();
    const Tick hitLen = std::clamp<Tick>(roundHalfUp(step), kMinNoteTicks, kPpq / 4);
    for (const auto& b : env.blocks()) {
        const Block& blk = env.block(b);
        std::vector<DrumHit> hits;
        if (blk.drums) {
            for (const auto& lane : *blk.drums) {
                auto pat = env.pattern(b, lane.steps, "xXg.-", toString(lane.voice));
                if (pat.empty()) continue;
                for (int bar = 0; bar < b.bars(); ++bar) {
                    for (int s = 0; s < pat.stepsPerBar; ++s) {
                        char t = pat.at(bar, s);
                        if (t != 'x' && t != 'X' && t != 'g') continue;
                        Tick raw = env.stepTick(b, bar * pat.stepsPerBar + s, pat);
                        if (env.owns(b.index, raw)) hits.push_back({raw, lane.voice, t, 1.0});
                    }
                }
            }
        } else if (!blk.notes && !(blk.fill && *blk.fill != Fill::None)) {
            env.warnOnce(env.where(b) + ": drums block has no lanes");
        }
        if (blk.fill && *blk.fill != Fill::None) {
            Tick lastBar = env.time.barStart(b.endBar);
            if (env.owns(b.index, lastBar)) FillBuilder(env, hits, lastBar).apply(*blk.fill);
        }
        std::stable_sort(hits.begin(), hits.end(), [](const DrumHit& x, const DrumHit& y) { return x.raw < y.raw; });
        for (const auto& h : hits) {
            RawNote n;
            n.tick = env.swing.apply(h.raw);
            n.pitch = drumVoiceNote(h.voice);
            n.fixedPitch = true;
            n.sublane = drumSublaneForNote(n.pitch);
            n.dur = (h.voice == DrumVoice::Crash || h.voice == DrumVoice::OpenHat) ? hitLen * 2 : hitLen;
            n.anchor = (h.voice == DrumVoice::Kick || h.voice == DrumVoice::Snare) &&
                       h.raw % env.time.ticksPerBeat() == 0;
            const double base = env.part.velocity * energyFactor(env.energyAt(h.raw));
            // Ghosts sit at a fixed fraction of the kit level, independent of the voice balance.
            double v = h.kind == 'g' ? base * kGhostFactor : base * voiceLevel(h.voice);
            v *= h.scale;
            if (h.kind == 'X') v += kAccentBoost;
            const double w = metricWeight(env.time, h.raw);
            if (isTimekeeper(h.voice) && h.kind != 'g') v += w >= 0.5 ? 5.0 : (w >= 0.0 ? 0.0 : -9.0);
            else if (!isTom(h.voice) && h.kind != 'g') v += w * 2.0;
            n.vel = clampVelocity(v);
            n.justified = true;
            out.push_back(n);
        }
    }
    return out;
}

}  // namespace flowstate::detail
