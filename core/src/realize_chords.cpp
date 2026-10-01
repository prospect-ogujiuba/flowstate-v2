// Chords and pad: rhythm hits over the harmony, voice-led voicings.
#include "flowstate/voicing.h"
#include "realize_internal.h"

#include <algorithm>
#include <map>

namespace flowstate::detail {

namespace {

struct Hit {
    Tick start = 0;
    Tick end = 0;
    bool accent = false;
    bool restrike = false;  // re-struck at a chord change inside a hold
    const ChordSpan* chord = nullptr;
    int block = 0;
    Articulation art = Articulation::Normal;
    Tick blockEnd = 0;
};

// Splits [s, e) at chord boundaries; gaps rest.
void splitAtChords(const PartEnv& env, Tick s, Tick e, bool accent, int block, Articulation art, Tick blockEnd,
                   std::vector<Hit>& out) {
    Tick t = s;
    while (t < e) {
        const ChordSpan* span = env.harmony.at(t);
        if (!span) {
            const ChordSpan* next = nullptr;
            for (const auto& sp : env.harmony.spans())
                if (sp.start > t) {
                    next = &sp;
                    break;
                }
            if (!next || next->start >= e) break;
            t = next->start;
            continue;
        }
        Tick segEnd = std::min(e, span->end);
        Hit h;
        h.start = t;
        h.end = segEnd;
        h.accent = accent && t == s;
        h.restrike = t != s;
        h.chord = span;
        h.block = block;
        h.art = art;
        h.blockEnd = blockEnd;
        out.push_back(h);
        t = segEnd;
    }
}

}  // namespace

std::vector<RawNote> realizeChordPart(const PartEnv& env) {
    const bool pad = env.part.role == Role::Pad;
    const VoicingFamily defaultFamily = pad ? VoicingFamily::Open : VoicingFamily::Close;
    const Articulation defaultArt = pad ? Articulation::Legato : Articulation::Normal;

    std::vector<Hit> hits;
    for (const auto& b : env.blocks()) {
        const Block& blk = env.block(b);
        const Articulation art = env.articulation(b, defaultArt);
        if (blk.rhythm) {
            auto pat = env.pattern(b, *blk.rhythm, "xX-.", "rhythm");
            for (const auto& ev : stepEvents(pat, b.bars())) {
                Tick s = env.swungStep(b, ev.step, pat);
                if (!env.owns(b.index, env.stepTick(b, ev.step, pat))) continue;
                Tick e = std::min(env.swungStep(b, ev.step + ev.length, pat), b.end);
                splitAtChords(env, s, e, ev.token == 'X', b.index, art, b.end, hits);
            }
        } else {
            // No rhythm: strike each chord and hold it.
            for (const auto& span : env.harmony.spans()) {
                Tick s = std::max(span.start, b.start);
                Tick e = std::min(span.end, b.end);
                if (s >= e || !env.owns(b.index, s)) continue;
                splitAtChords(env, s, e, false, b.index, Articulation::Legato, b.end, hits);
            }
        }
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.start < b.start; });

    // One voicing per (chord, block), voice-led across the whole part.
    std::map<std::pair<int, int>, std::size_t> stepOf;
    std::vector<VoicingStep> steps;
    for (const auto& h : hits) {
        auto key = std::make_pair(h.chord->index, h.block);
        if (stepOf.count(key)) continue;
        const auto& v = env.part.blocks[static_cast<std::size_t>(h.block)].voicing;
        stepOf[key] = steps.size();
        steps.push_back({h.chord->chord, v ? *v : defaultFamily, true});
    }
    auto voicings = voiceLead(steps, env.low, env.high, env.scale, &env.warnings);

    std::vector<RawNote> out;
    for (std::size_t i = 0; i < hits.size(); ++i) {
        const Hit& h = hits[i];
        const Voicing& v = voicings[stepOf[{h.chord->index, h.block}]];
        Tick span = h.end - h.start;
        Tick dur;
        if (h.art == Articulation::Legato) {
            Tick next = h.chord->end;
            if (i + 1 < hits.size()) next = std::min(next, hits[i + 1].start);
            next = std::min(next, h.blockEnd);
            dur = std::max(span, next - h.start);
        } else {
            dur = std::max<Tick>(1, static_cast<Tick>(static_cast<double>(span) * gateFor(h.art)));
        }
        int base = env.velocity(h.start, h.accent, false, pad ? 0.0 : 4.0);
        if (h.restrike) base -= pad ? 2 : 6;
        for (std::size_t k = 0; k < v.size(); ++k) {
            RawNote n;
            n.tick = h.start;
            n.dur = dur;
            n.pitch = v[k];
            int shape = 0;
            if (!pad) {
                if (k + 1 == v.size()) shape = 4;       // bring out the top voice
                else if (k == 0) shape = -3;            // soften the bottom
            }
            n.vel = clampVelocity(base + shape);
            n.justified = h.chord->chord.containsPc(v[k]);
            out.push_back(n);
        }
    }
    return out;
}

}  // namespace flowstate::detail
