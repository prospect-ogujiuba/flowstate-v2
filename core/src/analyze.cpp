// MIDI -> score IR analyzer. The profile and lane classifier follow v1's MidiContentProfiler and
// MidiLaneClassifier (same measures and golden thresholds); key, grid, chord naming, IR writing and the
// fidelity check are new. Everything is deterministic: same bytes and options, same analysis.
#include "flowstate/analyze.h"

#include "flowstate/output.h"
#include "flowstate/realize.h"
#include "flowstate/smf.h"
#include "flowstate/theory.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <sstream>

namespace flowstate {

using nlohmann::json;

namespace {

const char* const kSharpNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kFlatNames[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

std::string pcName(int pc, bool flats) { return (flats ? kFlatNames : kSharpNames)[mod12(pc)]; }

int percent(double v) { return std::clamp(static_cast<int>(std::lround(v)), 0, 100); }
double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double round3(double v) { return std::round(v * 1000.0) / 1000.0; }

// GM drum pitch -> IR voice. Pitches without a voice stay literal notes.
std::optional<DrumVoice> voiceForGmNote(int p) {
    switch (p) {
        case 35: case 36: return DrumVoice::Kick;
        case 38: case 40: return DrumVoice::Snare;
        case 39: return DrumVoice::Clap;
        case 37: return DrumVoice::Rim;
        case 42: return DrumVoice::ClosedHat;
        case 44: return DrumVoice::PedalHat;
        case 46: return DrumVoice::OpenHat;
        case 41: case 43: case 45: return DrumVoice::LowTom;
        case 47: case 48: return DrumVoice::MidTom;
        case 50: return DrumVoice::HighTom;
        case 49: case 52: case 55: case 57: return DrumVoice::Crash;
        case 51: case 59: return DrumVoice::Ride;
        case 53: return DrumVoice::RideBell;
        case 69: case 70: case 82: return DrumVoice::Shaker;
        case 54: return DrumVoice::Tambourine;
        case 56: return DrumVoice::Cowbell;
        default: return std::nullopt;
    }
}
bool isGmDrumPitch(int p) { return p >= 27 && p <= 87; }

// Flat spelling for keys that are written with flats.
bool prefersFlats(int tonic, Mode mode) {
    const bool minor = mode == Mode::Minor || mode == Mode::Dorian || mode == Mode::Phrygian || mode == Mode::Locrian ||
                       mode == Mode::HarmonicMinor || mode == Mode::MelodicMinor || mode == Mode::MinorPentatonic ||
                       mode == Mode::Blues;
    static const std::set<int> majorFlats{5, 10, 3, 8, 1, 6}, minorFlats{2, 7, 0, 5, 10, 3};
    return minor ? minorFlats.count(mod12(tonic)) > 0 : majorFlats.count(mod12(tonic)) > 0;
}

// Spells `pc` as the interval `iv` above a root spelled `root` ("E" + major third = "G#"), so slash
// basses read the way the chord does. Falls back to the plain name past a single accidental.
std::string spellAbove(const std::string& root, int pc, bool flats) {
    static const char kLetters[] = "CDEFGAB";
    static const int kNatural[] = {0, 2, 4, 5, 7, 9, 11};
    static const int kSteps[12] = {0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6};
    const int rootLetter = static_cast<int>(std::string(kLetters).find(root[0]));
    const int rootPc = pitchClassFromName(root);
    if (rootLetter < 0 || rootPc < 0) return pcName(pc, flats);
    const int letter = (rootLetter + kSteps[mod12(pc - rootPc)]) % 7;
    int acc = mod12(pc - kNatural[letter]);
    if (acc > 6) acc -= 12;
    if (acc < -1 || acc > 1) return pcName(pc, flats);
    return std::string(1, kLetters[letter]) + (acc < 0 ? "b" : acc > 0 ? "#" : "");
}

const char* modeName(Mode m) { return toString(m); }

// Beats as the IR writes them: ".5", "1", "1.5", or a fraction ("1/3") when not a short decimal.
std::string formatBeats(Tick ticks, Tick ticksPerBeat) {
    const Tick g = std::gcd(ticks, ticksPerBeat);
    const Tick num = ticks / g, den = ticksPerBeat / g;
    if (den == 1) return std::to_string(num);
    Tick d = den;
    while (d % 2 == 0) d /= 2;
    while (d % 5 == 0) d /= 5;
    if (d != 1 || den > 64) return std::to_string(num) + "/" + std::to_string(den);
    std::ostringstream os;
    os.precision(6);
    os << static_cast<double>(num) / static_cast<double>(den);
    std::string s = os.str();
    if (s.rfind("0.", 0) == 0) s.erase(0, 1);
    return s;
}

}  // namespace

// ---------- Profile and lane (v1's measures) ----------

MidiProfile profileNotes(const std::vector<MidiNote>& notes) {
    MidiProfile p;
    p.noteCount = static_cast<int>(notes.size());
    if (notes.empty()) return p;
    std::map<Tick, int> onsets;
    std::array<int, 128> pitchCounts{};
    std::array<int, 12> pcs{};
    int ch10 = 0, drum = 0, low = 0;
    double vel = 0.0;
    for (const auto& n : notes) {
        ++onsets[n.tick];
        ++pitchCounts[static_cast<std::size_t>(n.pitch)];
        ++pcs[static_cast<std::size_t>(mod12(n.pitch))];
        if (n.channel == 9) {
            ++ch10;
            if (isGmDrumPitch(n.pitch)) ++drum;
        }
        if (n.pitch >= kBassLowestPitch && n.pitch <= kBassHighestPitch) ++low;
        vel += n.vel;
    }
    const double count = static_cast<double>(notes.size());
    p.onsetCount = static_cast<int>(onsets.size());
    int mono = 0;
    for (const auto& [t, c] : onsets) mono += c == 1 ? 1 : 0;
    p.monoOnsetRatio = mono / static_cast<double>(p.onsetCount);
    p.polyOnsetRatio = 1.0 - p.monoOnsetRatio;
    p.notesPerOnset = count / p.onsetCount;
    p.channel10Ratio = ch10 / count;
    p.drumPitchRatio = drum / count;
    p.lowRegisterRatio = low / count;
    p.meanVelocity = vel / count;
    p.minPitch = 0;
    while (pitchCounts[static_cast<std::size_t>(p.minPitch)] == 0) ++p.minPitch;
    p.maxPitch = 127;
    while (pitchCounts[static_cast<std::size_t>(p.maxPitch)] == 0) --p.maxPitch;
    std::vector<int> sorted;
    for (const auto& n : notes) sorted.push_back(n.pitch);
    std::sort(sorted.begin(), sorted.end());
    p.medianPitch = (sorted[(sorted.size() - 1) / 2] + sorted[sorted.size() / 2]) / 2.0;
    for (int c : pcs) p.distinctPitchClasses += c > 0 ? 1 : 0;

    // Sounding time with one note vs several.
    std::vector<std::pair<Tick, int>> events;
    for (const auto& n : notes) {
        events.emplace_back(n.tick, 1);
        events.emplace_back(n.tick + n.dur, -1);
    }
    std::sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first < b.first : a.second < b.second;  // offs first
    });
    int active = 0;
    Tick prev = events.front().first;
    double monoT = 0.0, polyT = 0.0;
    for (const auto& [t, d] : events) {
        const double span = static_cast<double>(t - prev);
        if (active == 1) monoT += span;
        else if (active > 1) polyT += span;
        active += d;
        prev = t;
    }
    if (monoT + polyT > 0.0) {
        p.monoActivity = monoT / (monoT + polyT);
        p.polyActivity = polyT / (monoT + polyT);
    }
    return p;
}

LaneResult classifyLane(const MidiProfile& p) {
    const bool bassSafe = p.minPitch >= kBassLowestPitch && p.maxPitch <= kBassHighestPitch;
    const bool drumSafe = p.channel10Ratio > 0.0 && std::abs(p.channel10Ratio - p.drumPitchRatio) < 1e-9;
    const double pitched = 1.0 - p.channel10Ratio;
    const int width = p.maxPitch - p.minPitch;
    char buf[200];

    LaneResult r;
    {
        const double raw = 50.0 * p.channel10Ratio + 40.0 * p.drumPitchRatio + 10.0;
        int s = p.channel10Ratio <= 0.0 ? 0 : percent(raw);
        if (!drumSafe) s = std::min(s, 69);
        std::snprintf(buf, sizeof buf, "channel 10 %d%%; GM drum pitches %d%%", percent(p.channel10Ratio * 100),
                      percent(p.drumPitchRatio * 100));
        r.scores.push_back({Role::Drums, s, buf});
    }
    {
        const double raw = pitched * (35.0 * p.lowRegisterRatio +
                                      (p.medianPitch >= kBassLowestPitch && p.medianPitch <= kBassHighestPitch ? 20.0 : 0.0) +
                                      20.0 * p.monoOnsetRatio + 15.0 * p.monoActivity + (width <= 24 ? 10.0 : 0.0));
        int s = percent(raw);
        if (!bassSafe) s = std::min(s, 69);
        std::snprintf(buf, sizeof buf, "low register %d%%; median pitch %.1f; single-note onsets %d%%",
                      percent(p.lowRegisterRatio * 100), p.medianPitch, percent(p.monoOnsetRatio * 100));
        r.scores.push_back({Role::Bass, s, buf});
    }
    {
        const double chordDensity = clamp01((p.notesPerOnset - 1.0) / 2.0);
        const int s = percent(pitched * (30.0 * p.polyOnsetRatio + 30.0 * p.polyActivity + 25.0 * chordDensity +
                                         (width <= 24 ? 15.0 : 0.0)));
        std::snprintf(buf, sizeof buf, "chord onsets %d%%; %.1f notes per onset; several notes sounding %d%% of the time",
                      percent(p.polyOnsetRatio * 100), p.notesPerOnset, percent(p.polyActivity * 100));
        r.scores.push_back({Role::Chords, s, buf});
    }
    {
        const int s = percent(pitched * (30.0 * p.monoOnsetRatio + 30.0 * p.monoActivity + 25.0 * (1.0 - p.lowRegisterRatio) +
                                         (p.medianPitch > kBassHighestPitch ? 15.0 : 0.0)));
        std::snprintf(buf, sizeof buf, "single-note onsets %d%%; one note sounding %d%% of the time; median pitch %.1f",
                      percent(p.monoOnsetRatio * 100), percent(p.monoActivity * 100), p.medianPitch);
        r.scores.push_back({Role::Melody, s, buf});
    }
    std::stable_sort(r.scores.begin(), r.scores.end(), [](const LaneScore& a, const LaneScore& b) { return a.score > b.score; });
    const int top = r.scores[0].score, margin = top - r.scores[1].score;
    if (top >= kLanePrimaryScore && margin >= kLanePrimaryMargin) {
        r.primary = r.scores[0].role;
        r.highConfidence = top >= kLaneHighConfidence;
    }
    return r;
}

// ---------- Key ----------

KeyEstimate estimateKey(const std::vector<MidiNote>& notes) {
    static constexpr double kMajor[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static constexpr double kMinor[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    KeyEstimate k;
    std::array<double, 12> w{};
    std::map<Tick, int> lowest;
    for (const auto& n : notes) {
        auto [it, inserted] = lowest.emplace(n.tick, n.pitch);
        if (!inserted) it->second = std::min(it->second, n.pitch);
    }
    for (const auto& n : notes) {
        const double beats = std::min(4.0, static_cast<double>(n.dur) / kPpq);
        w[static_cast<std::size_t>(mod12(n.pitch))] += beats * (lowest[n.tick] == n.pitch ? 1.5 : 1.0);
    }
    auto pearson = [](const std::array<double, 12>& x, const double* y, int rot) {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) {
            mx += x[static_cast<std::size_t>(i)];
            my += y[i];
        }
        mx /= 12;
        my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i) {
            const double a = x[static_cast<std::size_t>(mod12(i + rot))] - mx, b = y[i] - my;
            sxy += a * b;
            sxx += a * a;
            syy += b * b;
        }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : 0.0;
    };
    struct Cand {
        int tonic;
        Mode mode;
        double r;
    };
    std::vector<Cand> cands;
    for (int t = 0; t < 12; ++t) {
        cands.push_back({t, Mode::Major, pearson(w, kMajor, t)});
        cands.push_back({t, Mode::Minor, pearson(w, kMinor, t)});
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.r > b.r; });
    const Cand& best = cands[0];
    const Cand& second = cands[1];
    k.tonic = best.tonic;
    k.mode = best.mode;
    k.correlation = best.r;
    k.margin = best.r - second.r;
    const bool flats = prefersFlats(best.tonic, best.mode);
    const std::string bestName = pcName(best.tonic, flats) + " " + modeName(best.mode);
    const std::string secondName =
        pcName(second.tonic, prefersFlats(second.tonic, second.mode)) + " " + modeName(second.mode);
    char buf[240];
    if (notes.size() < 8) {
        k.reason = "too few notes to tell the key";
    } else if (best.r < 0.70) {
        std::snprintf(buf, sizeof buf, "no clear tonal centre (best fit %s, r %.2f)", bestName.c_str(), best.r);
        k.reason = buf;
    } else if (k.margin < 0.05) {
        std::snprintf(buf, sizeof buf, "ambiguous between %s (r %.2f) and %s (r %.2f)", bestName.c_str(), best.r,
                      secondName.c_str(), second.r);
        k.reason = buf;
    } else {
        k.reliable = true;
        std::snprintf(buf, sizeof buf, "%s, r %.2f, ahead of %s by %.2f", bestName.c_str(), best.r, secondName.c_str(),
                      k.margin);
        k.reason = buf;
    }
    return k;
}

// ---------- Chord naming ----------

std::string nameChord(const std::vector<int>& pitches, bool flats) {
    if (pitches.empty()) return "";
    static const std::vector<std::string> kQualities = {
        "", "m", "5", "7", "maj7", "m7", "6", "m6", "69", "m69", "9", "maj9", "m9", "11", "m11", "13", "maj13", "m13",
        "add9", "madd9", "sus2", "sus4", "7sus4", "9sus4", "dim", "dim7", "m7b5", "aug", "7b9", "7#9", "7#11", "7b13",
        "maj7#11", "mMaj7", "7#5", "7b5"};
    std::set<int> present;
    for (int p : pitches) present.insert(mod12(p));
    std::vector<int> sorted = pitches;
    std::sort(sorted.begin(), sorted.end());
    const int bass = mod12(sorted.front());
    // A left-hand bass sits a fifth or more under the voicing; otherwise the lowest note is just the
    // bottom voice, and an inversion isn't named as a slash chord.
    const bool distinctBass = sorted.size() < 2 || sorted[1] - sorted[0] >= 7;

    std::string bestSymbol;
    double bestScore = -1e9;
    for (int root = 0; root < 12; ++root) {
        for (std::size_t qi = 0; qi < kQualities.size(); ++qi) {
            const std::string& q = kQualities[qi];
            const std::string base = pcName(root, flats) + q;
            const auto parsed = parseChord(base);
            if (!parsed.chord || !parsed.warning.empty()) continue;
            const Chord& c = *parsed.chord;
            std::set<int> tones;
            for (int pc : c.pitchClasses()) tones.insert(mod12(pc));
            double score = 0.0;
            int hits = 0, extra = 0;
            for (int pc : present) (tones.count(pc) ? hits : extra) += 1;
            score += 2.0 * hits - 2.5 * extra;
            for (int pc : tones) {
                if (present.count(pc)) continue;
                const int iv = mod12(pc - root);
                if (iv == 0) score -= 1.0;                                    // rootless voicing
                else if (iv == 7 && c.fifth == 7) score -= 0.5;               // omitted fifth
                else score -= 2.0;                                            // a colour the chord claims
            }
            score -= 0.03 * static_cast<double>(qi);  // simpler names win ties
            std::string symbol = base;
            if (bass == root) {
                score += distinctBass ? 1.5 : 0.5;
            } else if (tones.count(bass) && !distinctBass) {
                // the bottom voice of an inversion: no slash
            } else if (tones.count(bass)) {
                score += 0.9;  // an inversion: name the bass
                symbol += "/" + spellAbove(pcName(root, flats), bass, flats);
            } else if (present.count(bass)) {
                score += 2.5 - 0.6;  // the bass is not a chord tone: a slash chord explains it
                symbol += "/" + spellAbove(pcName(root, flats), bass, flats);
            }
            if (score > bestScore + 1e-9) {
                bestScore = score;
                bestSymbol = symbol;
            }
        }
    }
    return bestSymbol;
}

namespace {

// ---------- Grid ----------

struct Quantizer {
    Tick ticksPerBeat = kPpq;
    int grid = 4;
    double swing = 0.0;

    double step() const { return static_cast<double>(ticksPerBeat) / grid; }
    // Tick of a global step index, with swing on odd steps.
    double tickOf(long long idx) const {
        return (static_cast<double>(idx) + ((idx & 1) && swing > 0 ? swing : 0.0)) * step();
    }
    long long nearest(double t) const {
        const long long base = static_cast<long long>(std::floor(t / step()));
        long long best = base;
        double bestD = 1e18;
        for (long long i = base - 1; i <= base + 2; ++i) {
            const double d = std::abs(tickOf(i) - t);
            if (d < bestD - 1e-9) {
                bestD = d;
                best = i;
            }
        }
        return std::max<long long>(0, best);
    }
    double error(double t) const { return std::abs(tickOf(nearest(t)) - t); }
};

// Played onsets may sit up to a 64th note (60 ticks) off the grid.
double tolerance(const Quantizer& q) { return std::min(60.0, q.step() * 0.25); }

double gridFit(const Quantizer& q, const std::vector<Tick>& times, double tol) {
    if (times.empty()) return 1.0;
    int ok = 0;
    for (Tick t : times) ok += q.error(static_cast<double>(t)) <= tol ? 1 : 0;
    return ok / static_cast<double>(times.size());
}

GridEstimate estimateGrid(const std::vector<MidiNote>& notes, Tick ticksPerBeat, bool drums) {
    std::set<Tick> onsetSet, endSet;
    for (const auto& n : notes) {
        onsetSet.insert(n.tick);
        if (!drums) endSet.insert(n.tick + n.dur);
    }
    const std::vector<Tick> onsets(onsetSet.begin(), onsetSet.end()), ends(endSet.begin(), endSet.end());
    auto fits = [&](const Quantizer& q, double& fit) {
        // Note ends are looser than onsets: they only rule out a grid most of them miss.
        fit = gridFit(q, onsets, tolerance(q));
        return fit >= 0.95 && gridFit(q, ends, q.step() * 0.25) >= 0.6;
    };
    GridEstimate best{4, 0.0, -1.0}, found;
    auto consider = [&](int grid, double swing) {
        Quantizer q{ticksPerBeat, grid, swing};
        double fit = 0.0;
        const bool good = fits(q, fit);
        found = {grid, swing, fit};
        if (fit > best.fit + 1e-9) best = found;
        return good;
    };
    // Straight, then triplet, then swung, then 32nds; else the best fit.
    for (int g : {2, 4, 3, 6})
        if (consider(g, 0.0)) return found;
    for (int g : {2, 4})
        for (int s = 5; s <= 75; s += 5)
            if (consider(g, s / 100.0)) return found;
    for (int g : {8, 12})
        if (consider(g, 0.0)) return found;
    return best;
}

// ---------- Quantized notes ----------

struct QNote {
    long long step = 0;  // onset, global step from the clip start
    long long end = 0;   // exclusive
    int pitch = 0;
    int vel = 0;
    std::size_t source = 0;  // index into the original notes
};

std::vector<QNote> quantize(const std::vector<MidiNote>& notes, const Quantizer& q) {
    std::vector<QNote> out;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const auto& n = notes[i];
        QNote x;
        x.step = q.nearest(static_cast<double>(n.tick));
        x.end = std::max(x.step + 1, q.nearest(static_cast<double>(n.tick + n.dur)));
        x.pitch = n.pitch;
        x.vel = n.vel;
        x.source = i;
        out.push_back(x);
    }
    std::stable_sort(out.begin(), out.end(), [](const QNote& a, const QNote& b) {
        return a.step != b.step ? a.step < b.step : a.pitch < b.pitch;
    });
    return out;
}

// ---------- Step strings ----------

// Bars of step tokens -> "x--- ..x- | ..." with a space per beat and the shortest repeating bar period.
std::string stepString(const std::vector<std::string>& bars, int grid) {
    std::size_t period = bars.size();
    for (std::size_t p = 1; p < bars.size(); ++p) {
        if (bars.size() % p) continue;
        bool same = true;
        for (std::size_t i = p; i < bars.size() && same; ++i) same = bars[i] == bars[i % p];
        // A hold into the repeat must also be a hold in the original at that bar line.
        if (same) {
            period = p;
            break;
        }
    }
    std::string out;
    for (std::size_t b = 0; b < period; ++b) {
        if (b) out += " | ";
        for (std::size_t i = 0; i < bars[b].size(); ++i) {
            if (i && i % static_cast<std::size_t>(grid) == 0) out += ' ';
            out += bars[b][i];
        }
    }
    return out;
}

std::vector<std::string> emptyBars(int bars, int stepsPerBar) {
    return std::vector<std::string>(static_cast<std::size_t>(bars), std::string(static_cast<std::size_t>(stepsPerBar), '.'));
}

void setStep(std::vector<std::string>& bars, int stepsPerBar, long long step, char token) {
    const auto bar = static_cast<std::size_t>(step / stepsPerBar);
    if (bar >= bars.size()) return;
    bars[bar][static_cast<std::size_t>(step % stepsPerBar)] = token;
}

char stepAt(const std::vector<std::string>& bars, int stepsPerBar, long long step) {
    const auto bar = static_cast<std::size_t>(step / stepsPerBar);
    if (bar >= bars.size()) return '\0';
    return bars[bar][static_cast<std::size_t>(step % stepsPerBar)];
}

// Marks '-' holds after a strike, up to (not including) `end` and never over another strike.
void holdSteps(std::vector<std::string>& bars, int stepsPerBar, long long from, long long end) {
    for (long long s = from + 1; s < end; ++s) {
        const char c = stepAt(bars, stepsPerBar, s);
        if (c != '.' && c != '-') break;
        setStep(bars, stepsPerBar, s, '-');
    }
}

// ---------- Writers per role ----------

struct Writer {
    const std::vector<MidiNote>& notes;  // shifted to the clip start
    const Quantizer& q;
    int bars;
    int num;
    Tick ticksPerBeat;
    int stepsPerBar;
    int tonic;
    Mode mode;
    bool flats;
    double meanVel;
    std::vector<std::string>& warnings;
    std::vector<std::string> harmonySymbols;
    int literal = 0;

    long long clipSteps() const { return static_cast<long long>(bars) * stepsPerBar; }
    Tick stepTicks(long long steps) const {
        // Exact ticks for a step count (straight grid; used for beats in strings).
        return static_cast<Tick>(steps) * ticksPerBeat / q.grid;
    }
    std::string beats(long long steps) const { return formatBeats(stepTicks(steps), ticksPerBeat); }

    json literalNote(const MidiNote& n) const {
        const Tick tpb = ticksPerBeat * num;
        const int bar = static_cast<int>(n.tick / tpb) + 1;
        const double beat = static_cast<double>(n.tick - (bar - 1) * tpb) / static_cast<double>(ticksPerBeat) + 1.0;
        return json{{"bar", bar},
                    {"beat", std::round(beat * 10000.0) / 10000.0},
                    {"beats", std::round(static_cast<double>(n.dur) / static_cast<double>(ticksPerBeat) * 10000.0) / 10000.0},
                    {"pitch", midiToNoteName(n.pitch, flats)},
                    {"velocity", n.vel}};
    }

    // Harmony string from (start step, symbol) spans; the last span runs to the clip end.
    std::string harmonyString(const std::vector<std::pair<long long, std::string>>& spans) {
        std::string out;
        long long at = 0;
        for (std::size_t i = 0; i < spans.size(); ++i) {
            const long long start = spans[i].first;
            const long long end = i + 1 < spans.size() ? spans[i + 1].first : clipSteps();
            if (end <= start) continue;
            auto token = [&](const std::string& sym, long long from, long long to) {
                if (!out.empty()) out += from % stepsPerBar == 0 ? " | " : " ";
                out += sym + ":" + beats(to - from);
            };
            if (start > at) token("r", at, start);
            token(spans[i].second, start, end);
            harmonySymbols.push_back(spans[i].second);
            at = end;
        }
        return out;
    }

    json part(const char* id, Role role, const std::string& name, int low, int high, int velocity) const {
        return json{{"id", id},
                    {"role", toString(role)},
                    {"name", name},
                    {"low", midiToNoteName(low, flats)},
                    {"high", midiToNoteName(high, flats)},
                    {"grid", q.grid},
                    {"velocity", velocity},
                    {"blocks", json::array()}};
    }

    char strikeToken(int vel) const { return vel >= meanVel + 12.0 ? 'X' : 'x'; }

    // Chords: one harmony span per change of chord, a comping rhythm, and a voicing family picked later.
    void chords(json& score, json& part) {
        const auto qn = quantize(notes, q);
        std::map<long long, std::vector<const QNote*>> strikes;
        for (const auto& n : qn)
            if (n.step < clipSteps()) strikes[n.step].push_back(&n);
        std::vector<std::pair<long long, std::string>> spans;
        auto bars_ = emptyBars(bars, stepsPerBar);
        for (const auto& [step, group] : strikes) {
            std::vector<int> pitches;
            long long end = step + 1;
            double vel = 0;
            for (const auto* n : group) {
                pitches.push_back(n->pitch);
                end = std::max(end, n->end);
                vel += n->vel;
            }
            const std::string sym = nameChord(pitches, flats);
            if (spans.empty() || spans.back().second != sym) spans.emplace_back(step, sym);
            setStep(bars_, stepsPerBar, step, strikeToken(static_cast<int>(vel / static_cast<double>(group.size()))));
        }
        for (auto it = strikes.begin(); it != strikes.end(); ++it) {
            long long end = it->first + 1;
            for (const auto* n : it->second) end = std::max(end, n->end);
            holdSteps(bars_, stepsPerBar, it->first, std::min(end, clipSteps()));
        }
        score["harmony"] = harmonyString(spans);
        part["blocks"].push_back(json{{"startBar", 1}, {"endBar", bars}, {"rhythm", stepString(bars_, q.grid)},
                                      {"voicing", "close"}});
    }

    // Bass: one inferred chord per bar (or per run of equal bars), chord-relative tokens, literal notes
    // for tones the tokens can't name.
    void bass(json& score, json& part) {
        auto qn = quantize(notes, q);
        // Monophonic: the lowest note wins a shared onset; a note ends at the next onset.
        std::vector<QNote> line;
        for (const auto& n : qn) {
            if (n.step >= clipSteps()) continue;
            if (!line.empty() && line.back().step == n.step) continue;  // sorted by pitch: keep the lowest
            line.push_back(n);
        }
        for (std::size_t i = 0; i + 1 < line.size(); ++i) line[i].end = std::min(line[i].end, line[i + 1].step);
        const Scale scale = makeScale(tonic, mode);

        // Root per bar: duration-weighted pitch class, on-beat and downbeat notes counting more.
        std::vector<Chord> barChords;
        std::vector<std::string> barSymbols;
        for (int b = 0; b < bars; ++b) {
            std::array<double, 12> w{};
            bool any = false;
            for (const auto& n : line) {
                if (n.step / stepsPerBar != b) continue;
                any = true;
                const long long inBar = n.step % stepsPerBar;
                double weight = static_cast<double>(n.end - n.step);
                if (inBar % q.grid == 0) weight *= 2.0;
                if (inBar == 0) weight *= 2.0;
                w[static_cast<std::size_t>(mod12(n.pitch))] += weight;
            }
            if (!any) {
                barChords.push_back(barChords.empty() ? diatonicChord(scale, 1, false) : barChords.back());
                barSymbols.push_back(barSymbols.empty() ? "" : barSymbols.back());
                continue;
            }
            const int root = static_cast<int>(std::max_element(w.begin(), w.end()) - w.begin());
            std::string quality;
            int degree = 0;
            for (int d = 1; d <= scale.size(); ++d)
                if (mod12(scale.tonic + scale.degreeOffset(d)) == root) degree = d;
            if (degree > 0) {
                const Chord c = diatonicChord(scale, degree, false);
                quality = c.third == 3 ? (c.fifth == 6 ? "dim" : "m") : (c.fifth == 8 ? "aug" : "");
            } else {
                quality = w[static_cast<std::size_t>(mod12(root + 3))] > w[static_cast<std::size_t>(mod12(root + 4))] ? "m" : "";
            }
            const std::string sym = pcName(root, flats) + quality;
            barChords.push_back(*parseChord(sym).chord);
            barSymbols.push_back(sym);
        }
        // Bars before the first note take the first chord.
        for (int b = bars - 2; b >= 0; --b)
            if (barSymbols[static_cast<std::size_t>(b)].empty()) {
                barSymbols[static_cast<std::size_t>(b)] = barSymbols[static_cast<std::size_t>(b + 1)];
                barChords[static_cast<std::size_t>(b)] = barChords[static_cast<std::size_t>(b + 1)];
            }
        std::vector<std::pair<long long, std::string>> spans;
        for (int b = 0; b < bars; ++b)
            if (!barSymbols[static_cast<std::size_t>(b)].empty() &&
                (spans.empty() || spans.back().second != barSymbols[static_cast<std::size_t>(b)]))
                spans.emplace_back(static_cast<long long>(b) * stepsPerBar, barSymbols[static_cast<std::size_t>(b)]);
        score["harmony"] = harmonyString(spans);

        int lowestRoot = 127;
        for (const auto& n : line)
            if (mod12(n.pitch) == barChords[static_cast<std::size_t>(n.step / stepsPerBar)].bassRoot())
                lowestRoot = std::min(lowestRoot, n.pitch);

        auto bars_ = emptyBars(bars, stepsPerBar);
        json literals = json::array();
        for (std::size_t i = 0; i < line.size(); ++i) {
            const auto& n = line[i];
            const Chord& c = barChords[static_cast<std::size_t>(n.step / stepsPerBar)];
            const int iv = mod12(n.pitch - c.bassRoot());
            int seventh = c.guideSeventh();
            if (seventh < 0) seventh = scale.contains(c.root + 10) ? 10 : scale.contains(c.root + 11) ? 11 : -1;
            char token = 0;
            if (iv == 0) {
                if (n.pitch >= lowestRoot + 10) token = '8';
                else if (n.vel < meanVel * 0.6) token = 'g';
                else token = n.vel >= meanVel + 12.0 ? 'X' : 'R';
            } else if (iv == mod12(c.third) && c.third >= 0) {
                token = '3';
            } else if (iv == c.fifth && c.fifth >= 0) {
                token = '5';
            } else if (seventh >= 0 && iv == mod12(seventh)) {
                token = '7';
            } else if (i + 1 < line.size()) {
                const auto& next = line[i + 1];
                const auto bar = static_cast<std::size_t>(n.step / stepsPerBar);
                const auto nextBar = static_cast<std::size_t>(next.step / stepsPerBar);
                const Chord& nc = barChords[nextBar];
                if (barSymbols[nextBar] != barSymbols[bar] && mod12(next.pitch) == nc.bassRoot() &&
                    std::abs(next.pitch - n.pitch) == 1)
                    token = 'a';
            }
            if (token) {
                setStep(bars_, stepsPerBar, n.step, token);
            } else {
                literals.push_back(literalNote(notes[n.source]));
                ++literal;
            }
        }
        for (const auto& n : line)
            if (stepAt(bars_, stepsPerBar, n.step) != '.') holdSteps(bars_, stepsPerBar, n.step, std::min(n.end, clipSteps()));
        json block{{"startBar", 1}, {"endBar", bars}, {"rhythm", stepString(bars_, q.grid)}};
        if (!literals.empty()) block["notes"] = literals;
        part["blocks"].push_back(block);
    }

    // Melody: the top line as one motif in scale degrees.
    void melody(json& score, json& part, int low, int high) {
        auto qn = quantize(notes, q);
        std::vector<QNote> line;
        int dropped = 0;
        for (const auto& n : qn) {
            if (n.step >= clipSteps()) continue;
            if (!line.empty() && line.back().step == n.step) {
                line.back() = n;  // sorted by pitch: the highest wins
                ++dropped;
                continue;
            }
            line.push_back(n);
        }
        if (dropped > 0)
            warnings.push_back(std::to_string(dropped) + " notes under the top line are not in the melody's IR");
        for (std::size_t i = 0; i + 1 < line.size(); ++i) line[i].end = std::min(line[i].end, line[i + 1].step);

        const Scale scale = makeScale(tonic, mode);
        // The realizer's degree reference: the tonic pitch nearest the middle of the register.
        const double mid = (low + high) / 2.0;
        int centre = -1;
        for (int p = scale.tonic; p <= 127; p += 12)
            if (centre < 0 || std::abs(p - mid) < std::abs(centre - mid)) centre = p;
        auto degreeToken = [&](int pitch) {
            const int rel = pitch - centre;
            for (int alter : {0, -1, 1})
                for (int d = 1; d <= scale.size(); ++d) {
                    const int base = scale.degreeOffset(d) + alter;
                    if (mod12(base - rel) != 0) continue;
                    const int octave = (rel - base) / 12;
                    std::string t = alter < 0 ? "b" : alter > 0 ? "#" : "";
                    t += std::to_string(d);
                    t += std::string(static_cast<std::size_t>(std::abs(octave)), octave > 0 ? '+' : '-');
                    return t;
                }
            return std::string("1");
        };
        std::string motif;
        long long at = 0;
        auto add = [&](const std::string& pitch, long long from, long long to, bool accent) {
            if (!motif.empty()) motif += from % stepsPerBar == 0 ? " | " : " ";
            motif += pitch + ":" + beats(to - from) + (accent ? "!" : "");
        };
        for (const auto& n : line) {
            if (n.step > at) add("r", at, n.step, false);
            const long long end = std::min(n.end, clipSteps());
            add(degreeToken(n.pitch), n.step, end, n.vel >= meanVel + 12.0);
            at = end;
        }
        score["motifs"].push_back(json{{"id", "m1"}, {"notes", motif}});
        score["harmony"] = "";
        part["blocks"].push_back(json{{"startBar", 1}, {"endBar", bars}, {"motif", "m1"}, {"repeatEvery", 0}});
    }

    // Drums: one step string per IR voice; pitches without a voice stay literal.
    void drums(json& score, json& part) {
        const auto qn = quantize(notes, q);
        std::map<DrumVoice, std::vector<std::string>> lanes;
        json literals = json::array();
        for (const auto& n : qn) {
            if (n.step >= clipSteps()) continue;
            const auto voice = voiceForGmNote(n.pitch);
            if (!voice) {
                literals.push_back(literalNote(notes[n.source]));
                ++literal;
                continue;
            }
            auto& bars_ = lanes.try_emplace(*voice, emptyBars(bars, stepsPerBar)).first->second;
            const char token = n.vel <= meanVel * 0.6 ? 'g' : n.vel >= meanVel + 15.0 ? 'X' : 'x';
            const char prev = stepAt(bars_, stepsPerBar, n.step);
            if (prev == '.' || token == 'X' || (prev == 'g' && token == 'x')) setStep(bars_, stepsPerBar, n.step, token);
        }
        json lanesJson = json::array();
        for (const auto& [voice, bars_] : lanes) lanesJson.push_back(json{{"voice", toString(voice)}, {"steps", stepString(bars_, q.grid)}});
        json block{{"startBar", 1}, {"endBar", bars}, {"drums", lanesJson}, {"fill", "none"}};
        if (!literals.empty()) block["notes"] = literals;
        part["blocks"].push_back(block);
        score["harmony"] = "";
    }
};

// ---------- Fidelity ----------

Fidelity measure(const std::vector<MidiNote>& original, const std::vector<NoteEvent>& realized, Role role, double tol) {
    Fidelity f;
    const bool byClass = role == Role::Chords;
    auto onsetsOf = [&](auto const& xs) {
        std::vector<Tick> ts;
        for (const auto& n : xs) ts.push_back(n.tick);
        std::sort(ts.begin(), ts.end());
        std::vector<Tick> merged;
        for (Tick t : ts)
            if (merged.empty() || static_cast<double>(t - merged.back()) > tol) merged.push_back(t);
        return merged;
    };
    const auto oOn = onsetsOf(original), rOn = onsetsOf(realized);
    // Greedy in time order: each original onset takes the nearest unused realized onset within tolerance.
    std::vector<int> pairOf(oOn.size(), -1);
    std::vector<bool> used(rOn.size(), false);
    int matched = 0;
    for (std::size_t i = 0; i < oOn.size(); ++i) {
        int best = -1;
        double bestD = tol + 1e-9;
        for (std::size_t j = 0; j < rOn.size(); ++j) {
            if (used[j]) continue;
            const double d = std::abs(static_cast<double>(oOn[i] - rOn[j]));
            if (d <= bestD) {
                bestD = d;
                best = static_cast<int>(j);
            }
        }
        if (best >= 0) {
            used[static_cast<std::size_t>(best)] = true;
            pairOf[i] = best;
            ++matched;
        }
    }
    auto f1 = [](double m, double a, double b) { return a + b > 0 ? 2.0 * m / (a + b) : 1.0; };
    f.rhythm = f1(matched, static_cast<double>(oOn.size()), static_cast<double>(rOn.size()));

    auto groupAt = [&](auto const& xs, Tick at) {
        std::multiset<int> ps;
        for (const auto& n : xs)
            if (std::abs(static_cast<double>(n.tick - at)) <= tol) ps.insert(byClass ? mod12(n.pitch) : n.pitch);
        return ps;
    };
    double jaccard = 0.0;
    double noteHits = 0.0;
    for (std::size_t i = 0; i < oOn.size(); ++i) {
        if (pairOf[i] < 0) continue;
        const auto a = groupAt(original, oOn[i]);
        const auto b = groupAt(realized, rOn[static_cast<std::size_t>(pairOf[i])]);
        std::set<int> ac, bc;
        for (int p : a) ac.insert(mod12(p));
        for (int p : b) bc.insert(mod12(p));
        std::vector<int> inter, uni;
        std::set_intersection(ac.begin(), ac.end(), bc.begin(), bc.end(), std::back_inserter(inter));
        std::set_union(ac.begin(), ac.end(), bc.begin(), bc.end(), std::back_inserter(uni));
        jaccard += uni.empty() ? 1.0 : static_cast<double>(inter.size()) / static_cast<double>(uni.size());
        std::vector<int> common;
        std::set_intersection(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(common));
        noteHits += static_cast<double>(common.size());
    }
    f.pitch = matched ? jaccard / matched : 0.0;
    f.notes = f1(noteHits, static_cast<double>(original.size()), static_cast<double>(realized.size()));
    return f;
}

// A rolled or strummed chord: onsets chained less than a 64th note apart, within a 32nd overall, are
// one onset at the earliest of them. Note ends stay where they were. 32nd-note runs are not merged.
std::vector<MidiNote> alignRolledOnsets(std::vector<MidiNote> notes) {
    std::size_t i = 0;
    while (i < notes.size()) {
        std::size_t j = i + 1;
        while (j < notes.size() && notes[j].tick - notes[j - 1].tick <= 60 && notes[j].tick - notes[i].tick <= 120) ++j;
        for (std::size_t k = i + 1; k < j; ++k) {
            const Tick end = notes[k].tick + notes[k].dur;
            notes[k].tick = notes[i].tick;
            notes[k].dur = end - notes[k].tick;
        }
        i = j;
    }
    std::stable_sort(notes.begin(), notes.end(), [](const MidiNote& x, const MidiNote& y) {
        return x.tick != y.tick ? x.tick < y.tick : x.pitch < y.pitch;
    });
    return notes;
}

void setFailure(Analysis& a, std::string code, std::string message) {
    a.ok = false;
    a.errorCode = std::move(code);
    a.error = std::move(message);
}

std::string noteLabel(int pitch) { return midiToNoteName(pitch) + " (" + std::to_string(pitch) + ")"; }

// v1's lane sanity: bass stays in the bass window, drums are all channel 10.
std::optional<std::pair<std::string, std::string>> laneRuleFailure(const MidiProfile& p, Role lane) {
    if (lane == Role::Bass && (p.minPitch < kBassLowestPitch || p.maxPitch > kBassHighestPitch))
        return std::pair<std::string, std::string>{
            "bass_range", "notes from " + noteLabel(p.minPitch) + " to " + noteLabel(p.maxPitch) + " leave the bass range " +
                              noteLabel(kBassLowestPitch) + " to " + noteLabel(kBassHighestPitch)};
    if (lane == Role::Drums && p.channel10Ratio < 1.0)
        return std::pair<std::string, std::string>{
            "not_drums", std::to_string(percent((1.0 - p.channel10Ratio) * 100)) +
                             "% of the notes are not on MIDI channel 10, so they are not General MIDI drums"};
    return std::nullopt;
}

}  // namespace

Analysis analyzeMidi(const std::vector<std::uint8_t>& bytes, const AnalyzeOptions& options) {
    Analysis a;
    const auto read = readSmf(bytes);
    if (!read.data) {
        setFailure(a, toString(read.error->code), read.error->message);
        return a;
    }
    const MidiFileData& file = *read.data;
    a.warnings = file.warnings;
    a.tempoFromFile = file.tempo.has_value();
    a.tempo = file.tempo ? std::round(*file.tempo * 100.0) / 100.0 : 120.0;
    if (!file.tempo) a.warnings.push_back("no tempo in the file; 120 BPM assumed");
    a.meterNumerator = file.meterNumerator.value_or(4);
    a.meterDenominator = file.meterDenominator.value_or(4);
    if (!file.meterNumerator) a.warnings.push_back("no time signature in the file; 4/4 assumed");
    const Tick ticksPerBeat = static_cast<Tick>(kPpq) * 4 / a.meterDenominator;
    const Tick ticksPerBar = ticksPerBeat * a.meterNumerator;

    // Leading empty bars are trimmed; the clip ends at the bar after the last note, or at the file's end
    // when that is a whole number of bars no more than twice as long (a loop with a rest at its end).
    std::vector<MidiNote> notes = file.notes;
    Tick first = notes.front().tick, last = 0;
    for (const auto& n : notes) last = std::max(last, n.tick + n.dur);
    const Tick shift = (first / ticksPerBar) * ticksPerBar;
    if (shift > 0) {
        a.warnings.push_back(std::to_string(shift / ticksPerBar) + " empty bars before the first note were trimmed");
        for (auto& n : notes) n.tick -= shift;
    }
    const Tick slack = ticksPerBar / 16;
    int bars = static_cast<int>(std::max<Tick>(1, (last - shift - slack + ticksPerBar - 1) / ticksPerBar));
    const Tick fileEnd = file.endTick - shift;
    if (fileEnd % ticksPerBar == 0) {
        const int fileBars = static_cast<int>(fileEnd / ticksPerBar);
        if (fileBars > bars && fileBars <= bars * 2) bars = fileBars;
    }
    if (bars > 64) {
        setFailure(a, "too_long", "the clip is " + std::to_string(bars) + " bars long; the library takes up to 64");
        return a;
    }
    a.bars = bars;
    const Tick clipEnd = ticksPerBar * bars;
    for (auto& n : notes) n.dur = std::max<Tick>(1, std::min(n.dur, clipEnd - n.tick));
    notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const MidiNote& n) { return n.tick >= clipEnd; }), notes.end());

    const std::vector<MidiNote> exact = notes;
    notes = alignRolledOnsets(std::move(notes));
    a.profile = profileNotes(notes);
    a.lanes = classifyLane(a.profile);
    const auto& top = a.lanes.scores.front();

    // Lane: the manifest's, unless the content clearly says otherwise; v1's safety rules still apply.
    if (options.declaredRole) {
        const Role declared = *options.declaredRole;
        if (declared != Role::Chords && declared != Role::Bass && declared != Role::Melody && declared != Role::Drums) {
            setFailure(a, "bad_lane", std::string("lane ") + toString(declared) + " is not a library lane (chords, bass, melody, drums)");
            return a;
        }
        // The declared lane's own rule is the more useful reason, so it goes before a conflict.
        if (auto f = laneRuleFailure(a.profile, declared)) {
            setFailure(a, f->first, f->second);
            return a;
        }
        if (a.lanes.primary && *a.lanes.primary != declared && a.lanes.highConfidence) {
            setFailure(a, "lane_conflict",
                       std::string("the manifest says ") + toString(declared) + ", but the content is clearly " +
                           toString(*a.lanes.primary) + " (score " + std::to_string(top.score) + ": " + top.evidence + ")");
            return a;
        }
        if (a.lanes.primary && *a.lanes.primary != declared)
            a.warnings.push_back(std::string("the content leans ") + toString(*a.lanes.primary) + " (score " +
                                 std::to_string(top.score) + "); kept as " + toString(declared) + " per the manifest");
        a.role = declared;
    } else if (a.lanes.primary) {
        a.role = *a.lanes.primary;
    } else {
        a.role = top.role;
        a.warnings.push_back(std::string("the lane is unclear; ") + toString(top.role) + " scored highest (" +
                             std::to_string(top.score) + ")");
    }
    if (auto f = laneRuleFailure(a.profile, a.role)) {
        setFailure(a, f->first, f->second);
        return a;
    }
    if (a.role != Role::Drums && a.profile.channel10Ratio > 0.0) {
        setFailure(a, "mixed_drums", "the clip mixes channel 10 drums with pitched notes; split it into one file per lane");
        return a;
    }

    const bool drums = a.role == Role::Drums;
    if (drums) {
        a.key.reason = "drums have no key";
    } else {
        a.key = estimateKey(notes);
        if (options.keyTonic && options.keyMode) {
            const KeyEstimate detected = a.key;
            a.key.reliable = true;
            a.key.tonic = mod12(*options.keyTonic);
            a.key.mode = *options.keyMode;
            a.key.reason = "from the manifest (detection: " + detected.reason + ")";
        }
    }
    a.grid = estimateGrid(notes, ticksPerBeat, drums);
    const Quantizer q{ticksPerBeat, a.grid.grid, a.grid.swing};
    if (a.grid.fit < 0.95) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "only %d%% of onsets sit on a grid; the IR quantizes the rest", percent(a.grid.fit * 100));
        a.warnings.push_back(buf);
    }

    // ---- IR ----
    const int tonic = a.key.tonic;
    const Mode mode = a.key.mode;
    const bool flats = prefersFlats(tonic, mode);
    const std::string partName = options.partName.empty() ? std::string(toString(a.role)) : options.partName;
    json score{{"ir", kIrId},
               {"title", options.title},
               {"context", {{"tempo", a.tempo},
                            {"meterNumerator", a.meterNumerator},
                            {"meterDenominator", a.meterDenominator},
                            {"tonic", pcName(tonic, flats)},
                            {"mode", modeName(mode)},
                            {"bars", bars},
                            {"swing", a.grid.swing},
                            {"style", options.style}}},
               {"form", json::array({json{{"name", "A"}, {"startBar", 1}, {"bars", bars}, {"energy", 0.6}}})},
               {"harmony", ""},
               {"motifs", json::array()},
               {"parts", json::array()}};
    Writer w{notes, q, bars, a.meterNumerator, ticksPerBeat, a.meterNumerator * a.grid.grid, tonic, mode, flats,
             a.profile.meanVelocity, a.warnings, {}, 0};
    const int velocity = std::clamp(static_cast<int>(std::lround(a.profile.meanVelocity)), 1, 127);
    const char* partId = drums ? "drums" : toString(a.role);
    json part = w.part(partId, a.role, partName, a.profile.minPitch, a.profile.maxPitch, velocity);
    switch (a.role) {
        case Role::Chords: w.chords(score, part); break;
        case Role::Bass: w.bass(score, part); break;
        case Role::Melody: w.melody(score, part, a.profile.minPitch, a.profile.maxPitch); break;
        default: w.drums(score, part); break;
    }
    score["parts"].push_back(part);
    a.harmony = w.harmonySymbols;
    a.fidelity.literalNotes = w.literal;

    // Realize without humanizing and compare with the original. Chords try every voicing family and
    // keep the one closest to the original voicing.
    const double tol = std::max(30.0, q.step() * 0.3);
    auto realizeScore = [&](const json& s) {
        RealizeOptions ro;
        ro.humanize = false;
        return realizeJson(s.dump(), ro);
    };
    auto partNotes = [](const Realization& r) { return r.parts.empty() ? std::vector<NoteEvent>{} : r.parts.front().notes; };
    std::vector<std::string> realizeWarnings;
    try {
        if (a.role == Role::Chords) {
            // Exact-pitch agreement picks the family; pitch classes are the reported measure.
            double bestKey = -1.0;
            json best = score;
            for (const char* family : {"close", "open", "drop2", "drop3", "rootless", "shell", "spread", "quartal"}) {
                json s = score;
                s["parts"][0]["blocks"][0]["voicing"] = family;
                const Realization r = realizeScore(s);
                const Fidelity f = measure(notes, partNotes(r), Role::Chords, tol);
                const Fidelity byPitch = measure(notes, partNotes(r), Role::Bass, tol);
                // A family that has to fall back for some chords plays something else than it says.
                const auto fallbacks = std::count_if(r.warnings.begin(), r.warnings.end(), [](const std::string& warning) {
                    return warning.find("fallback voicing") != std::string::npos;
                });
                const double key = byPitch.notes + 0.001 * f.notes - 0.02 * static_cast<double>(fallbacks);
                if (key > bestKey + 1e-9) {
                    bestKey = key;
                    best = s;
                    a.fidelity.rhythm = f.rhythm;
                    a.fidelity.pitch = f.pitch;
                    a.fidelity.notes = f.notes;
                    realizeWarnings = r.warnings;
                }
            }
            score = best;
        } else {
            const Realization r = realizeScore(score);
            const Fidelity f = measure(notes, partNotes(r), a.role, tol);
            a.fidelity.rhythm = f.rhythm;
            a.fidelity.pitch = f.pitch;
            a.fidelity.notes = f.notes;
            realizeWarnings = r.warnings;
        }
    } catch (const IrError& e) {
        setFailure(a, "internal", std::string("the analyzer wrote IR that core rejects: ") + e.what());
        return a;
    }
    for (const auto& rw : realizeWarnings) a.warnings.push_back("IR: " + rw);
    a.scoreJson = score.dump();

    // ---- Descriptors ----
    {
        const auto qn = quantize(notes, q);
        std::set<long long> onsetSteps;
        std::set<long long> durations;
        for (const auto& n : qn) {
            onsetSteps.insert(n.step);
            durations.insert(n.end - n.step);
        }
        const double beatsTotal = static_cast<double>(bars) * a.meterNumerator;
        const double perBeat = static_cast<double>(onsetSteps.size()) / beatsTotal;
        a.descriptors.density = perBeat / (perBeat + 1.0);
        int off = 0;
        for (long long s : onsetSteps) off += s % a.grid.grid != 0 ? 1 : 0;
        a.descriptors.syncopation = onsetSteps.empty() ? 0.0 : off / static_cast<double>(onsetSteps.size());
        double colour = 0.0;
        if (a.role == Role::Chords) {
            std::map<long long, std::set<int>> strikes;
            for (const auto& n : qn) strikes[n.step].insert(mod12(n.pitch));
            for (const auto& [s, pcs] : strikes) colour += clamp01((static_cast<double>(pcs.size()) - 3.0) / 3.0);
            colour /= std::max<std::size_t>(1, strikes.size());
        } else if (drums) {
            std::set<int> voices;
            for (const auto& n : notes) voices.insert(n.pitch);
            colour = clamp01(static_cast<double>(voices.size()) / 8.0);
        } else {
            const Scale scale = makeScale(tonic, mode);
            int outside = 0, leaps = 0;
            for (std::size_t i = 0; i < qn.size(); ++i) {
                outside += scale.contains(qn[i].pitch) ? 0 : 1;
                if (i && std::abs(qn[i].pitch - qn[i - 1].pitch) > 4) ++leaps;
            }
            colour = clamp01(0.5 * outside / static_cast<double>(qn.size()) * 4.0 + 0.5 * leaps / static_cast<double>(qn.size()));
        }
        const double variety = clamp01((static_cast<double>(durations.size()) - 1.0) / 5.0);
        a.descriptors.complexity = clamp01(0.4 * a.descriptors.syncopation + 0.35 * colour + 0.25 * variety);
        a.descriptors.energy = clamp01(0.5 * a.profile.meanVelocity / 127.0 + 0.5 * a.descriptors.density);
        a.descriptors.groove = a.grid.swing > 0.0 ? "swing" : (a.grid.grid % 3 == 0 ? "triplet" : "straight");
    }

    // ---- Normalized MIDI: the original notes, exact, at 960 PPQ in a clip of exactly `bars` ----
    {
        Realization r;
        r.title = options.title;
        r.bars = bars;
        r.ticksPerBar = ticksPerBar;
        r.tempo = a.tempo;
        r.meterNumerator = a.meterNumerator;
        r.meterDenominator = a.meterDenominator;
        PartRealization p;
        p.id = partId;
        p.role = a.role;
        p.name = partName;
        p.channel = drums ? 9 : 0;
        for (const auto& n : exact) p.notes.push_back(NoteEvent{n.tick, n.dur, n.pitch, n.vel, drums ? drumSublaneForNote(n.pitch) : ""});
        std::stable_sort(p.notes.begin(), p.notes.end(), [](const NoteEvent& x, const NoteEvent& y) {
            return x.tick != y.tick ? x.tick < y.tick : x.pitch < y.pitch;
        });
        r.parts.push_back(std::move(p));
        a.normalizedMidi = writeSmf(r);
    }
    a.ok = true;
    return a;
}

std::string analysisJson(const Analysis& a, int indent) {
    json j{{"ok", a.ok}};
    if (!a.ok) {
        j["error"] = {{"code", a.errorCode}, {"message", a.error}};
        j["warnings"] = a.warnings;
        if (a.profile.noteCount > 0) {
            json lanes = json::array();
            for (const auto& s : a.lanes.scores) lanes.push_back({{"role", toString(s.role)}, {"score", s.score}, {"evidence", s.evidence}});
            j["lanes"] = lanes;
        }
        return j.dump(indent);
    }
    json lanes = json::array();
    for (const auto& s : a.lanes.scores) lanes.push_back({{"role", toString(s.role)}, {"score", s.score}, {"evidence", s.evidence}});
    const bool flats = prefersFlats(a.key.tonic, a.key.mode);
    j["role"] = toString(a.role);
    j["detectedRole"] = a.lanes.primary ? json(toString(*a.lanes.primary)) : json(nullptr);
    j["lanes"] = lanes;
    j["key"] = {{"tonic", a.key.reliable ? json(pcName(a.key.tonic, flats)) : json(nullptr)},
                {"mode", a.key.reliable ? json(modeName(a.key.mode)) : json(nullptr)},
                {"guess", a.role == Role::Drums ? json(nullptr) : json(pcName(a.key.tonic, flats) + " " + modeName(a.key.mode))},
                {"correlation", round3(a.key.correlation)},
                {"margin", round3(a.key.margin)},
                {"reason", a.key.reason}};
    j["grid"] = {{"grid", a.grid.grid}, {"swing", a.grid.swing}, {"fit", round3(a.grid.fit)}};
    j["descriptors"] = {{"density", round3(a.descriptors.density)},
                        {"complexity", round3(a.descriptors.complexity)},
                        {"energy", round3(a.descriptors.energy)},
                        {"syncopation", round3(a.descriptors.syncopation)},
                        {"groove", a.descriptors.groove}};
    j["profile"] = {{"notes", a.profile.noteCount},
                    {"onsets", a.profile.onsetCount},
                    {"minPitch", a.profile.minPitch},
                    {"maxPitch", a.profile.maxPitch},
                    {"meanVelocity", round3(a.profile.meanVelocity)},
                    {"notesPerOnset", round3(a.profile.notesPerOnset)}};
    j["tempo"] = a.tempo;
    j["tempoFromFile"] = a.tempoFromFile;
    j["meter"] = {a.meterNumerator, a.meterDenominator};
    j["bars"] = a.bars;
    j["harmony"] = a.harmony;
    j["fidelity"] = {{"rhythm", round3(a.fidelity.rhythm)},
                     {"pitch", round3(a.fidelity.pitch)},
                     {"notes", round3(a.fidelity.notes)},
                     {"literalNotes", a.fidelity.literalNotes}};
    j["score"] = json::parse(a.scoreJson);
    j["warnings"] = a.warnings;
    return j.dump(indent);
}

}  // namespace flowstate
