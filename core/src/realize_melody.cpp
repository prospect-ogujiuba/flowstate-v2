// Melody and counter: motifs (degrees + transforms) or the instant-sketch
// stepwise line over chord tones.
#include "realize_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace flowstate::detail {

namespace {

constexpr double kPi = 3.14159265358979323846;

// Locale-independent number parser for transform arguments ("+2", "-0.5").
bool parseNumber(const std::string& s, double& out) {
    std::size_t i = 0;
    double sign = 1.0;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) sign = s[i++] == '-' ? -1.0 : 1.0;
    double v = 0.0;
    bool digits = false;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        v = v * 10.0 + (s[i++] - '0');
        digits = true;
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        double scale = 0.1;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            v += (s[i++] - '0') * scale;
            scale *= 0.1;
            digits = true;
        }
    }
    if (!digits || i != s.size()) return false;
    out = sign * v;
    return true;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<MotifNote> applyTransforms(std::vector<MotifNote> notes, const std::vector<std::string>& transforms,
                                       const PartEnv& env, const std::string& where) {
    for (const auto& raw : transforms) {
        std::string t = trim(raw);
        std::string name = t, arg;
        auto colon = t.find(':');
        if (colon != std::string::npos) {
            name = trim(t.substr(0, colon));
            arg = trim(t.substr(colon + 1));
        }
        for (auto& ch : name) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        double value = 0.0;
        const bool hasValue = !arg.empty() && parseNumber(arg, value);
        if (!arg.empty() && !hasValue) {
            env.warnings.push_back(where + ": transform \"" + raw + "\" has an invalid argument; ignored");
            continue;
        }
        if (notes.empty()) continue;
        if (name == "transpose") {
            int n = static_cast<int>(std::lround(hasValue ? value : 0.0));
            for (auto& m : notes) m.degree += n;
        } else if (name == "invert") {
            auto first = std::min_element(notes.begin(), notes.end(),
                                          [](const MotifNote& a, const MotifNote& b) { return a.beat < b.beat; });
            const int d0 = first->degree, o0 = first->octave;
            for (auto& m : notes) {
                m.degree = 2 * d0 - m.degree;
                m.octave = 2 * o0 - m.octave;
                m.alter = -m.alter;
            }
        } else if (name == "retrograde") {
            double span = 0.0;
            for (const auto& m : notes) span = std::max(span, m.beat + m.beats);
            for (auto& m : notes) m.beat = span - (m.beat + m.beats);
        } else if (name == "displace") {
            for (auto& m : notes) m.beat += hasValue ? value : 0.0;
        } else if (name == "augment") {
            double f = hasValue && value > 0 ? value : 2.0;
            for (auto& m : notes) {
                m.beat *= f;
                m.beats *= f;
            }
        } else if (name == "diminish") {
            double f = hasValue && value > 0 ? value : 2.0;
            for (auto& m : notes) {
                m.beat /= f;
                m.beats /= f;
            }
        } else if (name == "octave") {
            int n = static_cast<int>(std::lround(hasValue ? value : 0.0));
            for (auto& m : notes) m.octave += n;
        } else {
            env.warnings.push_back(where + ": unknown transform \"" + raw + "\"; ignored");
        }
    }
    std::stable_sort(notes.begin(), notes.end(),
                     [](const MotifNote& a, const MotifNote& b) { return a.beat < b.beat; });
    return notes;
}

int nearestWithPc(int pc, double ref, int low, int high) {
    int best = -1;
    for (int p = mod12(pc); p <= 127; p += 12) {
        if (p < low || p > high) continue;
        if (best < 0 || std::abs(p - ref) < std::abs(best - ref)) best = p;
    }
    if (best < 0) best = foldPitch(mod12(pc) + 60, low, high);
    return best;
}

void realizeMotifBlock(const PartEnv& env, const BlockSpan& b, const Motif& motif, std::vector<RawNote>& out) {
    const Block& blk = env.block(b);
    auto notes = applyTransforms(motif.notes, blk.transforms ? *blk.transforms : std::vector<std::string>{}, env,
                                 env.where(b));
    if (notes.empty()) return;
    const int centre = env.registerCentre();
    auto pitchOf = [&](const MotifNote& m) {
        return centre + env.scale.degreeOffset(m.degree) + 12 * m.octave + m.alter;
    };
    // Keep the motif's contour: shift the whole motif by octaves when that
    // puts more notes in range, before any per-note folding.
    int bestShift = 0, bestOut = 1 << 30;
    for (int shift : {0, -12, 12, -24, 24}) {
        int outCount = 0;
        for (const auto& m : notes) {
            int p = pitchOf(m) + shift;
            if (p < env.low || p > env.high) ++outCount;
        }
        if (outCount < bestOut) {
            bestOut = outCount;
            bestShift = shift;
        }
    }
    const double blockBeats = static_cast<double>(b.end - b.start) / static_cast<double>(env.time.ticksPerBeat());
    double period = blk.repeatEvery ? *blk.repeatEvery : 0.0;
    if (period > 0.0 && period < 0.0625) {
        env.warnOnce(env.where(b) + ": repeatEvery too small; motif played once");
        period = 0.0;
    }
    const Articulation art = env.articulation(b, Articulation::Normal);
    const double gate = art == Articulation::Normal ? 0.92 : gateFor(art);
    for (int rep = 0;; ++rep) {
        const double offset = rep * period;
        if (offset >= blockBeats || (rep > 0 && period <= 0.0)) break;
        for (const auto& m : notes) {
            if (m.beats <= 0.0) continue;
            Tick raw = b.start + env.time.beatsToTicks(offset + m.beat);
            if (raw < 0 || raw >= b.end) continue;
            if (raw >= b.start && !env.owns(b.index, raw)) continue;
            Tick rawEnd = std::min(b.start + env.time.beatsToTicks(offset + m.beat + m.beats), b.end);
            Tick s = env.swing.apply(raw);
            Tick e = env.swing.apply(rawEnd);
            RawNote n;
            n.tick = s;
            n.dur = std::max<Tick>(1, static_cast<Tick>(static_cast<double>(e - s) * gate));
            n.pitch = pitchOf(m) + bestShift;
            n.vel = env.velocity(raw, m.accent, false, 3.0);
            n.justified = m.alter != 0;
            out.push_back(n);
        }
    }
}

void realizeSketchBlock(const PartEnv& env, const BlockSpan& b, const StepPattern& pat, std::vector<RawNote>& out,
                        int& prevPitch) {
    const bool counter = env.part.role == Role::Counter;
    const Articulation art = env.articulation(b, Articulation::Normal);
    auto events = stepEvents(pat, b.bars());
    const double centre = std::clamp(env.registerCentre(), env.low, env.high);
    const Tick phraseLen = env.time.ticksPerBar() * 2;
    const Chord tonicTriad = diatonicChord(env.scale, 1, false);
    int p = prevPitch;
    int dir = counter ? -1 : 1;
    int repeats = 0;
    for (std::size_t i = 0; i < events.size(); ++i) {
        const auto& ev = events[i];
        const Tick raw = env.stepTick(b, ev.step);
        if (!env.owns(b.index, raw)) continue;
        const ChordSpan* span = env.harmony.at(raw);
        const Chord& chord = span ? span->chord : tonicTriad;
        const auto pcs = chord.pitchClasses();
        auto isChordTone = [&](int q) { return std::find(pcs.begin(), pcs.end(), mod12(q)) != pcs.end(); };

        const double rel = static_cast<double>((raw - b.start) % phraseLen) / static_cast<double>(phraseLen);
        const double arch = std::sin(kPi * rel);
        const double target = counter ? centre - 4.0 * arch + 1.0 : centre + 5.0 * arch - 1.0;
        const Tick nextRaw = i + 1 < events.size() ? env.stepTick(b, events[i + 1].step) : b.end;
        const bool phraseEnd = i + 1 == events.size() ||
                               (nextRaw - b.start) / phraseLen != (raw - b.start) / phraseLen;
        const bool strong = metricWeight(env.time, raw) >= 0.5 || ev.token == 'X';

        int pitch;
        if (p < 0) {
            // First note: the chord tone nearest the contour start.
            int best = -1;
            for (int q = env.low; q <= env.high; ++q)
                if (isChordTone(q) && (best < 0 || std::abs(q - target) < std::abs(best - target))) best = q;
            pitch = best >= 0 ? best : static_cast<int>(centre);
        } else if (std::string("R3578").find(ev.token) != std::string::npos) {
            int iv = 0;
            switch (ev.token) {
                case '3': iv = chord.third >= 0 ? chord.third : 7; break;
                case '5': iv = chord.fifth >= 0 ? chord.fifth : 7; break;
                case '7': iv = chord.guideSeventh() >= 0 ? chord.guideSeventh() : 10; break;
                default: iv = 0; break;
            }
            pitch = nearestWithPc(chord.root + iv, p, env.low, env.high);
            if (ev.token == '8') {
                int r = nearestWithPc(chord.root, p - 6, env.low, env.high);
                pitch = r + 12 <= env.high ? r + 12 : r;
            }
        } else {
            if (target - p > 1.5) dir = 1;
            else if (target - p < -1.5) dir = -1;
            if (phraseEnd) {
                // Cadence: settle on the root or third, nearest.
                int r = nearestWithPc(chord.root, p, env.low, env.high);
                int t3 = chord.third >= 0 ? nearestWithPc(chord.root + chord.third, p, env.low, env.high) : r;
                pitch = std::abs(t3 - p) < std::abs(r - p) ? t3 : r;
            } else if (strong) {
                int best = -1;
                double bestCost = 1e9;
                for (int q = p - 5; q <= p + 5; ++q) {
                    if (q < env.low || q > env.high || !isChordTone(q)) continue;
                    double cost = std::abs(q - (p + dir * 2.0));
                    if (q == p) cost += 2.5;
                    if ((q - p) * dir < 0) cost += 1.5;
                    if (cost < bestCost) {
                        bestCost = cost;
                        best = q;
                    }
                }
                pitch = best >= 0 ? best : (dir > 0 ? env.scale.stepUp(p) : env.scale.stepDown(p));
            } else {
                pitch = dir > 0 ? env.scale.stepUp(p) : env.scale.stepDown(p);
            }
            if (pitch > env.high) {
                dir = -1;
                pitch = env.scale.stepDown(p);
            } else if (pitch < env.low) {
                dir = 1;
                pitch = env.scale.stepUp(p);
            }
        }
        if (p >= 0 && pitch == p) {
            if (++repeats >= 2) {
                pitch = dir > 0 ? env.scale.stepUp(p) : env.scale.stepDown(p);
                if (pitch > env.high || pitch < env.low) pitch = dir > 0 ? env.scale.stepDown(p) : env.scale.stepUp(p);
                repeats = 0;
            }
        } else {
            repeats = 0;
        }
        if (p >= 0) {
            if (pitch > p) dir = 1;
            else if (pitch < p) dir = -1;
        }
        p = pitch;

        const Tick s = env.swungStep(b, ev.step);
        const Tick e = std::min(env.swungStep(b, ev.step + ev.length), b.end);
        RawNote n;
        n.tick = s;
        if (art == Articulation::Legato) {
            Tick next = i + 1 < events.size() ? env.swungStep(b, events[i + 1].step) : b.end;
            n.dur = std::max(e - s, std::min(next, b.end) - s);
        } else {
            n.dur = std::max<Tick>(1, static_cast<Tick>(static_cast<double>(e - s) * gateFor(art)));
        }
        n.pitch = pitch;
        n.vel = clampVelocity(env.velocity(raw, ev.token == 'X', false, 4.0) + std::round(4.0 * arch));
        n.justified = isChordTone(pitch);
        out.push_back(n);
    }
    prevPitch = p;
}

}  // namespace

std::vector<RawNote> realizeMelodyPart(const PartEnv& env) {
    std::vector<RawNote> out;
    int prevPitch = -1;
    for (const auto& b : env.blocks()) {
        const Block& blk = env.block(b);
        if (blk.motif) {
            if (const Motif* m = env.findMotif(*blk.motif)) {
                if (blk.rhythm) env.warnOnce(env.where(b) + ": motif set, rhythm ignored");
                realizeMotifBlock(env, b, *m, out);
                continue;
            }
            env.warnOnce(env.where(b) + ": motif \"" + *blk.motif + "\" not found");
        }
        if (blk.rhythm) {
            auto pat = env.pattern(b, *blk.rhythm, "xX-.R3578", "rhythm");
            realizeSketchBlock(env, b, pat, out, prevPitch);
        } else if (!blk.notes) {
            env.warnOnce(env.where(b) + ": no motif or rhythm; using a quarter-note sketch");
            StepPattern pat;
            pat.stepsPerBar = env.time.stepsPerBar(env.part.grid);
            std::string bar;
            for (int beat = 0; beat < env.time.numerator(); ++beat) {
                bar += 'x';
                bar.append(static_cast<std::size_t>(env.part.grid - 1), '-');
            }
            pat.bars = {bar};
            realizeSketchBlock(env, b, pat, out, prevPitch);
        }
    }
    return out;
}

}  // namespace flowstate::detail
