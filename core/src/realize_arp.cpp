// Arpeggiator: rhythm steps walk a voice-led chord-tone pool.
#include "realize_internal.h"

#include <algorithm>
#include <cmath>

namespace flowstate::detail {

namespace {

std::vector<int> arpTones(const Chord& c, bool coreOnly) {
    std::vector<int> iv{0};
    if (c.third >= 0) iv.push_back(c.third);
    if (c.fifth >= 0) iv.push_back(c.fifth);
    if (c.guideSeventh() >= 0) iv.push_back(c.guideSeventh());
    if (!coreOnly && !c.tensions.empty()) iv.push_back(c.tensions.back());
    std::vector<int> pcs;
    for (int i : iv) pcs.push_back(mod12(c.root + i));
    return pcs;
}

// Ascending pool of chord tones starting at a root placed near `anchor`.
std::vector<int> buildPool(const Chord& c, bool coreOnly, int low, int high, int& anchor) {
    const auto pcs = arpTones(c, coreOnly);
    auto isTone = [&](int p) { return std::find(pcs.begin(), pcs.end(), mod12(p)) != pcs.end(); };
    int best = -1;
    for (int p = mod12(c.root); p <= high; p += 12) {
        if (p < low) continue;
        if (best < 0 || std::abs(p - anchor) < std::abs(best - anchor)) best = p;
    }
    if (best < 0) best = low;
    anchor = best;
    std::vector<int> pool;
    for (int p = best; p <= high && p < best + 24 && pool.size() < 8; ++p)
        if (isTone(p)) pool.push_back(p);
    // Not enough room above: extend downward.
    for (int p = best - 1; p >= low && pool.size() < 4; --p)
        if (isTone(p)) pool.insert(pool.begin(), p);
    if (pool.empty()) pool.push_back(std::clamp(best, low, high));
    return pool;
}

}  // namespace

std::vector<RawNote> realizeArpPart(const PartEnv& env) {
    std::vector<RawNote> out;
    Rng rng(env.seed);
    int anchor = env.low + (env.high - env.low) / 4;
    long counter = 0;
    int lastIndex = -1;
    const int spb = env.time.stepsPerBar(env.part.grid);

    for (const auto& b : env.blocks()) {
        const Block& blk = env.block(b);
        const ArpPattern pattern = blk.arpPattern ? *blk.arpPattern : ArpPattern::Up;
        const Articulation art = env.articulation(b, Articulation::Normal);
        StepPattern pat;
        if (blk.rhythm) pat = env.pattern(b, *blk.rhythm, "xX-.", "rhythm");
        else {
            // No rhythm: every grid step.
            pat.stepsPerBar = spb;
            pat.bars = {std::string(static_cast<std::size_t>(spb), 'x')};
        }
        auto events = stepEvents(pat, b.bars());
        const ChordSpan* lastChord = nullptr;
        std::vector<int> pool;
        for (std::size_t e = 0; e < events.size(); ++e) {
            const auto& ev = events[e];
            Tick raw = env.stepTick(b, ev.step);
            if (!env.owns(b.index, raw)) continue;
            const ChordSpan* span = env.harmony.at(raw);
            if (!span) continue;
            if (span != lastChord) {
                pool = buildPool(span->chord, pattern == ArpPattern::ChordTones, env.low, env.high, anchor);
                lastChord = span;
            }
            const int n = static_cast<int>(pool.size());
            int idx = 0;
            switch (pattern) {
                case ArpPattern::Up: idx = static_cast<int>(counter % n); break;
                case ArpPattern::Down: idx = n - 1 - static_cast<int>(counter % n); break;
                case ArpPattern::UpDown: {
                    int period = n > 1 ? 2 * n - 2 : 1;
                    int k = static_cast<int>(counter % period);
                    idx = k < n ? k : period - k;
                    break;
                }
                case ArpPattern::Random: {
                    idx = rng.below(n);
                    if (n > 1 && idx == lastIndex) idx = (idx + 1 + rng.below(n - 1)) % n;
                    break;
                }
                case ArpPattern::ChordTones: {
                    // Broken-chord (Alberti) figure: low, high, middle, high.
                    static const int figure[4] = {0, 2, 1, 2};
                    idx = std::min(n - 1, figure[counter % 4]);
                    if (n >= 4 && counter % 8 == 7) idx = 3;  // touch the 7th every other figure
                    break;
                }
            }
            lastIndex = idx;
            ++counter;

            Tick s = env.swungStep(b, ev.step);
            Tick end = std::min(env.swungStep(b, ev.step + ev.length), b.end);
            Tick dur;
            if (art == Articulation::Legato) {
                Tick next = b.end;
                if (e + 1 < events.size()) next = env.swungStep(b, events[e + 1].step);
                dur = std::max(end - s, std::min(next, b.end) - s);
            } else {
                dur = std::max<Tick>(1, static_cast<Tick>(static_cast<double>(end - s) * gateFor(art)));
            }
            RawNote note;
            note.tick = s;
            note.dur = dur;
            note.pitch = pool[static_cast<std::size_t>(idx)];
            // Arp contour: slight lift toward the top of the pool.
            double lift = n > 1 ? 4.0 * idx / (n - 1) - 2.0 : 0.0;
            note.vel = clampVelocity(env.velocity(raw, ev.token == 'X', false, 5.0) + lift);
            note.justified = true;
            out.push_back(note);
        }
    }
    return out;
}

}  // namespace flowstate::detail
