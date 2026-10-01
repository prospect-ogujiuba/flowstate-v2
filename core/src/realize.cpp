// Realization orchestration: per-part environment, role dispatch, literal
// notes, humanize and the constraint pipeline.
#include "flowstate/realize.h"

#include "realize_internal.h"

#include <algorithm>
#include <cmath>

namespace flowstate {

namespace detail {

PartEnv::PartEnv(const Score& s, const Part& p, const TimeGrid& t, const Harmony& h, const Scale& sc,
                 std::uint64_t sd, std::vector<std::string>& w)
    : score(s),
      part(p),
      time(t),
      harmony(h),
      scale(sc),
      low(std::max(0, noteNameToMidi(p.low))),
      high(std::max(0, noteNameToMidi(p.high))),
      swing(t, p.grid, s.context.swing),
      seed(sd),
      warnings(w) {
    barOwner_.assign(static_cast<std::size_t>(t.bars()) + 2, -1);
    for (std::size_t i = 0; i < p.blocks.size(); ++i) {
        const Block& b = p.blocks[i];
        if (b.endBar < b.startBar || b.startBar > t.bars() || b.endBar < 1) continue;
        BlockSpan span;
        span.index = static_cast<int>(i);
        span.startBar = std::max(1, b.startBar);
        span.endBar = std::min(t.bars(), b.endBar);
        span.start = t.barStart(span.startBar);
        span.end = t.barStart(span.endBar + 1);
        blocks_.push_back(span);
        for (int bar = span.startBar; bar <= span.endBar; ++bar)
            barOwner_[static_cast<std::size_t>(bar)] = span.index;
    }
}

bool PartEnv::owns(int blockIndex, Tick t) const {
    if (t < 0 || t >= time.clipEnd()) return false;
    auto bar = static_cast<std::size_t>(t / time.ticksPerBar() + 1);
    return barOwner_[bar] == blockIndex;
}

double PartEnv::energyAt(Tick t) const {
    int bar = static_cast<int>(t / time.ticksPerBar()) + 1;
    for (const auto& sec : score.form)
        if (bar >= sec.startBar && bar < sec.startBar + sec.bars) return sec.energy;
    return 0.6;
}

int PartEnv::velocity(Tick t, bool accent, bool ghost, double metricScale) const {
    double v = part.velocity * energyFactor(energyAt(t));
    if (ghost) v *= kGhostFactor;
    else if (accent) v += kAccentBoost;
    v += metricWeight(time, t) * metricScale;
    return clampVelocity(v);
}

Tick PartEnv::stepTick(const BlockSpan& b, int globalStep, int scale) const {
    const int grid = part.grid * std::max(1, scale);
    const int spb = time.stepsPerBar(grid);
    const int bar = globalStep / spb;
    const int idx = globalStep % spb;
    return b.start + static_cast<Tick>(bar) * time.ticksPerBar() + time.stepOffset(idx, grid);
}

StepPattern PartEnv::pattern(const BlockSpan& b, const std::string& text, const std::string& allowed,
                             const std::string& label) const {
    return parseSteps(text, time.stepsPerBar(part.grid), allowed, where(b) + " " + label, warnings);
}

std::string PartEnv::where(const BlockSpan& b) const {
    return "part " + part.id + " block " + std::to_string(b.startBar) + ".." + std::to_string(b.endBar);
}

void PartEnv::warnOnce(const std::string& msg) const {
    if (warned_.insert(msg).second) warnings.push_back(msg);
}

const Motif* PartEnv::findMotif(const std::string& id) const {
    for (const auto& m : score.motifs)
        if (m.id == id) return &m;
    return nullptr;
}

int PartEnv::registerCentre() const {
    const double mid = (low + high) / 2.0;
    int best = -1;
    for (int p = scale.tonic; p <= 127; p += 12)
        if (best < 0 || std::abs(p - mid) < std::abs(best - mid)) best = p;
    return best;
}

Articulation PartEnv::articulation(const BlockSpan& b, Articulation fallback) const {
    const auto& a = block(b).articulation;
    return a ? *a : fallback;
}

double gateFor(Articulation a) {
    switch (a) {
        case Articulation::Legato: return 1.0;
        case Articulation::Normal: return 0.9;
        case Articulation::Staccato: return 0.5;
    }
    return 0.9;
}

std::vector<RawNote> realizeLiteralNotes(const PartEnv& env) {
    std::vector<RawNote> out;
    const bool drums = env.part.role == Role::Drums;
    for (const auto& b : env.blocks()) {
        const auto& lits = env.block(b).notes;
        if (!lits) continue;
        for (const auto& ln : *lits) {
            int pitch = noteNameToMidi(ln.pitch);
            if (pitch < 0) {
                env.warnings.push_back(env.where(b) + ": literal note pitch \"" + ln.pitch + "\" invalid; skipped");
                continue;
            }
            if (ln.bar < b.startBar || ln.bar > b.endBar)
                env.warnOnce(env.where(b) + ": literal note in bar " + std::to_string(ln.bar) +
                             " is outside the block; kept at its absolute bar");
            RawNote n;
            n.tick = env.time.at(ln.bar, ln.beat);
            n.dur = std::max<Tick>(1, env.time.beatsToTicks(ln.beats));
            n.pitch = pitch;
            n.vel = std::clamp(ln.velocity, 1, 127);
            n.justified = true;
            if (drums) {
                n.fixedPitch = true;
                n.sublane = drumSublaneForNote(pitch);
            }
            out.push_back(n);
        }
    }
    return out;
}

}  // namespace detail

namespace {

using detail::PartEnv;

void humanize(std::vector<RawNote>& notes, const TimeGrid& time, Role role, Rng& rng) {
    sortNotes(notes);
    const double maxT = time.msToTicks(8.0);
    const double spreadT = time.msToTicks(1.5);
    const Tick last = time.clipEnd() - 1;
    const bool drums = role == Role::Drums;
    const double timingScale = role == Role::Pad ? 0.5 : 1.0;
    std::size_t i = 0;
    while (i < notes.size()) {
        std::size_t j = i;
        while (j < notes.size() && notes[j].tick == notes[i].tick) ++j;
        // Pitched parts: a chord moves together, with a tiny per-note spread.
        const double shared = drums ? 0.0 : rng.triangular() * maxT * timingScale;
        for (std::size_t k = i; k < j; ++k) {
            auto& n = notes[k];
            double shift = drums ? rng.triangular() * maxT * (n.anchor ? 0.25 : 1.0)
                                 : shared * (n.anchor ? 0.25 : 1.0) + rng.triangular() * spreadT;
            Tick t = std::clamp<Tick>(n.tick + roundHalfUp(shift), 0, last);
            n.tick = t;
            double vr = n.vel < 50 ? 3.0 : 6.0;
            n.vel = clampVelocity(n.vel + std::round(rng.triangular() * vr));
        }
        i = j;
    }
}

}  // namespace

int drumVoiceNote(DrumVoice voice) {
    switch (voice) {
        case DrumVoice::Kick: return 36;
        case DrumVoice::Snare: return 38;
        case DrumVoice::Clap: return 39;
        case DrumVoice::Rim: return 37;
        case DrumVoice::ClosedHat: return 42;
        case DrumVoice::PedalHat: return 44;
        case DrumVoice::OpenHat: return 46;
        case DrumVoice::LowTom: return 45;
        case DrumVoice::MidTom: return 47;
        case DrumVoice::HighTom: return 50;
        case DrumVoice::Crash: return 49;
        case DrumVoice::Ride: return 51;
        case DrumVoice::RideBell: return 53;
        case DrumVoice::Shaker: return 70;
        case DrumVoice::Tambourine: return 54;
        case DrumVoice::Cowbell: return 56;
    }
    return 36;
}

const char* drumSublaneForNote(int n) {
    switch (n) {
        case 35: case 36: return "kick";
        case 38: case 40: return "snare";
        case 37: case 39: return "clap_rim";
        case 42: case 44: case 46: return "hats";
        case 41: case 43: case 45: case 47: case 48: case 50: return "toms";
        case 49: case 51: case 52: case 53: case 55: case 57: case 59: return "cymbals";
        default: return "aux_kit";
    }
}

Realization realize(const Score& score, const RealizeOptions& options) {
    Realization r;
    r.title = score.title;
    r.bars = score.context.bars;
    r.tempo = score.context.tempo;
    r.meterNumerator = score.context.meterNumerator;
    r.meterDenominator = score.context.meterDenominator;

    TimeGrid time(score.context);
    r.ticksPerBar = time.ticksPerBar();
    Scale scale = makeScale(score.context);
    Harmony harmony(score.harmony, time, scale, r.warnings);

    int nextChannel = 0;
    for (std::size_t pi = 0; pi < score.parts.size(); ++pi) {
        const Part& part = score.parts[pi];
        const std::string salt = part.id + "#" + std::to_string(pi);
        PartEnv env(score, part, time, harmony, scale, mixSeed(options.seed, "realize:" + salt), r.warnings);

        std::vector<RawNote> notes;
        switch (part.role) {
            case Role::Chords:
            case Role::Pad: notes = detail::realizeChordPart(env); break;
            case Role::Arp: notes = detail::realizeArpPart(env); break;
            case Role::Bass: notes = detail::realizeBassPart(env); break;
            case Role::Melody:
            case Role::Counter: notes = detail::realizeMelodyPart(env); break;
            case Role::Drums: notes = detail::realizeDrumPart(env); break;
        }
        auto literal = detail::realizeLiteralNotes(env);
        notes.insert(notes.end(), literal.begin(), literal.end());

        const bool drums = part.role == Role::Drums;
        const bool mono = isPitchedMonophonic(part.role);
        if (!drums) {
            int unfit = foldRange(notes, env.low, env.high);
            if (unfit > 0)
                r.warnings.push_back("part " + part.id + ": range " + part.low + ".." + part.high +
                                     " is too narrow; " + std::to_string(unfit) + " notes clamped");
        }
        // Exact duplicates (e.g. a fill hit on an existing hit) merge before
        // humanize so they cannot turn into flams.
        dedupe(notes);
        if (options.humanize) {
            Rng rng(mixSeed(options.seed, "humanize:" + salt));
            humanize(notes, time, part.role, rng);
        }
        clipToClip(notes, time.clipEnd());
        dedupe(notes);
        if (mono) enforceMonophony(notes, part.role == Role::Bass);
        enforceMinLength(notes, time.clipEnd(), mono);
        sortNotes(notes);

        if (!drums)
            for (const RawNote* n : findOutOfKey(notes, scale)) r.outOfKey.push_back({part.id, n->tick, n->pitch});

        PartRealization pr;
        pr.id = part.id;
        pr.role = part.role;
        pr.name = part.name;
        pr.low = env.low;
        pr.high = env.high;
        if (drums) {
            pr.channel = 9;
        } else {
            if (nextChannel == 9) ++nextChannel;
            if (nextChannel > 15) {
                r.warnings.push_back("part " + part.id + ": more than 15 pitched parts; MIDI channels reused");
                nextChannel = 0;
            }
            pr.channel = nextChannel++;
        }
        for (const auto& n : notes) pr.notes.push_back({n.tick, n.dur, n.pitch, n.vel, n.sublane});
        r.parts.push_back(std::move(pr));
    }
    return r;
}

Realization realizeJson(const std::string& jsonText, const RealizeOptions& options) {
    std::vector<std::string> warnings;
    Score score = parseScore(jsonText, warnings);
    Realization r = realize(score, options);
    warnings.insert(warnings.end(), r.warnings.begin(), r.warnings.end());
    r.warnings = std::move(warnings);
    return r;
}

}  // namespace flowstate
