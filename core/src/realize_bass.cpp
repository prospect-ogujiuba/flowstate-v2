// Bass: chord-relative step tokens (R 3 5 7 8 a x X g) anchored low and
// voice-led root to root.
#include "realize_internal.h"

#include <algorithm>
#include <cmath>

namespace flowstate::detail {

namespace {

class BassLine {
public:
    BassLine(const PartEnv& env) : env_(env) {
        // Roots live in the lower part of the register.
        roomTop_ = env.low + std::max(7, (env.high - env.low) * 6 / 10);
        anchor_ = env.low + std::max(5, (env.high - env.low) * 35 / 100);
        prev_ = anchor_;
    }

    // Root placement for a chord (slash bass when present), voice-led.
    int rootFor(const Chord& c, int reference) const {
        const int pc = c.bassRoot();
        int best = -1;
        double bestCost = 1e9;
        for (int p = pc; p <= env_.high; p += 12) {
            if (p < env_.low) continue;
            double cost = std::abs(p - reference);
            if (p > roomTop_) cost += (p - roomTop_) * 1.5;
            if (cost < bestCost) {
                bestCost = cost;
                best = p;
            }
        }
        if (best < 0) best = foldPitch(pc + 36, env_.low, env_.high);
        return best;
    }

    void enterChord(const ChordSpan* span) {
        if (span == current_) return;
        current_ = span;
        anchor_ = rootFor(span->chord, anchor_);
    }

    int anchor() const { return anchor_; }
    int prev() const { return prev_; }
    void played(int p) { prev_ = p; }

    // A chord tone placed for a smooth line: nearest the previous note,
    // preferring the octave above the root and the low part of the register.
    int tone(int pc) const {
        int best = -1;
        double bestCost = 1e9;
        for (int p = mod12(pc); p <= env_.high; p += 12) {
            if (p < env_.low) continue;
            double cost = std::abs(p - prev_);
            if (p < anchor_) cost += 2.0;
            if (p > anchor_ + 12) cost += 3.0;
            if (p > roomTop_) cost += (p - roomTop_) * 0.5;
            if (cost < bestCost) {
                bestCost = cost;
                best = p;
            }
        }
        return best >= 0 ? best : foldPitch(anchor_ + mod12(pc - anchor_), env_.low, env_.high);
    }

private:
    const PartEnv& env_;
    const ChordSpan* current_ = nullptr;
    int roomTop_;
    int anchor_;
    int prev_;
};

int seventhInterval(const Chord& c, const Scale& scale) {
    if (c.guideSeventh() >= 0) return c.guideSeventh();
    if (scale.contains(c.root + 10)) return 10;
    if (scale.contains(c.root + 11)) return 11;
    return 12;
}

}  // namespace

std::vector<RawNote> realizeBassPart(const PartEnv& env) {
    std::vector<RawNote> out;
    BassLine line(env);
    const double step = env.stepTicks();

    for (const auto& b : env.blocks()) {
        const Block& blk = env.block(b);
        const Articulation art = env.articulation(b, Articulation::Normal);
        struct Ev {
            char token;
            Tick raw, start, end;
        };
        std::vector<Ev> evs;
        if (blk.rhythm) {
            auto pat = env.pattern(b, *blk.rhythm, "R3578axXg-.", "rhythm");
            for (const auto& ev : stepEvents(pat, b.bars())) {
                Tick raw = env.stepTick(b, ev.step);
                if (!env.owns(b.index, raw)) continue;
                evs.push_back({ev.token, raw, env.swungStep(b, ev.step),
                               std::min(env.swungStep(b, ev.step + ev.length), b.end)});
            }
        } else {
            // No rhythm: root on every chord change, held.
            for (const auto& span : env.harmony.spans()) {
                Tick s = std::max(span.start, b.start);
                Tick e = std::min(span.end, b.end);
                if (s < e && env.owns(b.index, s)) evs.push_back({'R', s, s, e});
            }
        }
        for (std::size_t i = 0; i < evs.size(); ++i) {
            const Ev& ev = evs[i];
            const ChordSpan* span = env.harmony.at(ev.raw);
            RawNote n;
            n.tick = ev.start;
            n.justified = true;
            const bool ghost = ev.token == 'g';
            const bool accent = ev.token == 'X';
            if (ev.token == 'a') {
                // Chromatic approach into the next chord's root.
                const ChordSpan* next = env.harmony.nextAfter(ev.raw);
                if (!next) continue;
                int ref = span ? line.anchor() : line.prev();
                int target = line.rootFor(next->chord, ref);
                int p = line.prev() <= target ? target - 1 : target + 1;
                if (p == line.prev()) p = 2 * target - p;  // never repeat into the approach
                if (p < env.low) p = target + 1;
                if (p > env.high) p = target - 1;
                n.pitch = p;
            } else {
                if (!span) continue;  // chord gap: bass rests
                line.enterChord(span);
                const Chord& c = span->chord;
                switch (ev.token) {
                    case '3': n.pitch = line.tone(c.root + (c.third >= 0 ? c.third : 7)); break;
                    case '5': n.pitch = line.tone(c.root + (c.fifth >= 0 ? c.fifth : 7)); break;
                    case '7': {
                        int iv = seventhInterval(c, env.scale);
                        n.pitch = iv == 12 ? line.anchor() + 12 : line.tone(c.root + iv);
                        n.justified = c.containsPc(c.root + iv);
                        break;
                    }
                    case '8': n.pitch = line.anchor() + 12 <= env.high ? line.anchor() + 12 : line.anchor(); break;
                    default: n.pitch = line.anchor(); break;  // R x X g
                }
            }
            Tick held = ev.end - ev.start;
            if (ghost) {
                n.dur = std::max<Tick>(kMinNoteTicks, static_cast<Tick>(std::min(static_cast<double>(held), step) * 0.5));
            } else if (art == Articulation::Legato) {
                Tick next = i + 1 < evs.size() ? evs[i + 1].start : b.end;
                n.dur = std::max(held, next - ev.start);
            } else {
                n.dur = std::max<Tick>(1, static_cast<Tick>(static_cast<double>(held) * gateFor(art)));
            }
            n.vel = env.velocity(ev.raw, accent, ghost, 4.0);
            if (ev.token == 'a') n.vel = clampVelocity(n.vel - 6);
            line.played(n.pitch);
            out.push_back(n);
        }
    }
    return out;
}

}  // namespace flowstate::detail
