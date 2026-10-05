// Density (P1-21): thins or fills step patterns and motifs by metric strength. Used by every role
// realizer for a part's `density` and the live density knob; 0.5 leaves everything as written.
#include "realize_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace flowstate::detail {

namespace {

// How far below the beat a tick sits: 0 on the beat, 1 on an eighth, 2 on a sixteenth, 3 finer or
// off the binary grid (triplets).
int depthOf(const TimeGrid& time, Tick inBar) {
    const Tick beat = time.ticksPerBeat();
    const Tick f = inBar % beat;
    if (f == 0) return 0;
    if ((f * 2) % beat == 0) return 1;
    if ((f * 4) % beat == 0) return 2;
    return 3;
}

// Higher = metrically stronger: the bar downbeat, then beats (pulses), eighths, sixteenths.
double strengthAt(const TimeGrid& time, Tick inBar) { return metricWeight(time, inBar) - 0.1 * depthOf(time, inBar); }

bool pitched(DensityKind k) { return k == DensityKind::Chords || k == DensityKind::Bass || k == DensityKind::Line; }

bool onset(char t) { return t != '.' && t != '-'; }

struct Step {
    int bar = 0;
    int step = 0;
    double score = 0.0;
};

void thinPattern(StepPattern& pat, double f, DensityKind kind, const TimeGrid& time, int grid, Rng& rng) {
    const int g = grid * std::max(1, pat.scale);
    std::vector<Step> onsets;
    std::vector<std::size_t> keepIndex(pat.bars.size(), SIZE_MAX);
    for (std::size_t b = 0; b < pat.bars.size(); ++b) {
        for (int s = 0; s < pat.stepsPerBar; ++s) {
            const char t = pat.bars[b][static_cast<std::size_t>(s)];
            const double jitter = rng.uniform() * 0.01;  // drawn for every step, so ties stay put as tokens change
            if (!onset(t)) continue;
            double score = strengthAt(time, time.stepOffset(s, g)) + jitter;
            if (t == 'X') score += 0.3;
            if (t == 'g') score -= 10.0;
            const std::size_t i = onsets.size();
            onsets.push_back({static_cast<int>(b), s, score});
            if (keepIndex[b] == SIZE_MAX || score > onsets[keepIndex[b]].score) keepIndex[b] = i;
        }
    }
    std::vector<Step> removable;
    for (std::size_t i = 0; i < onsets.size(); ++i)
        if (keepIndex[static_cast<std::size_t>(onsets[i].bar)] != i) removable.push_back(onsets[i]);
    std::stable_sort(removable.begin(), removable.end(), [](const Step& a, const Step& b) { return a.score < b.score; });
    const auto n = static_cast<std::size_t>(std::lround(f * static_cast<double>(removable.size())));
    std::vector<std::vector<bool>> gone(pat.bars.size(), std::vector<bool>(static_cast<std::size_t>(pat.stepsPerBar), false));
    for (std::size_t i = 0; i < n && i < removable.size(); ++i)
        gone[static_cast<std::size_t>(removable[i].bar)][static_cast<std::size_t>(removable[i].step)] = true;

    if (!pitched(kind)) {
        for (std::size_t b = 0; b < pat.bars.size(); ++b)
            for (int s = 0; s < pat.stepsPerBar; ++s)
                if (gone[b][static_cast<std::size_t>(s)]) pat.bars[b][static_cast<std::size_t>(s)] = '.';
        return;
    }
    // Pitched: a removed onset becomes a hold when a note sounds into it, so the note before it lasts
    // longer (fewer, longer notes); otherwise a rest. The pattern repeats, so the first step follows
    // the last: one pass finds what sounds at the end, the second rewrites.
    bool sounding = false;
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t b = 0; b < pat.bars.size(); ++b) {
            for (int s = 0; s < pat.stepsPerBar; ++s) {
                char& t = pat.bars[b][static_cast<std::size_t>(s)];
                char now = t;
                if (gone[b][static_cast<std::size_t>(s)]) now = sounding ? '-' : '.';
                if (now == '.') sounding = false;
                else if (now != '-') sounding = true;
                if (pass == 1) t = now;
            }
        }
    }
}

void fillPattern(StepPattern& pat, double f, DensityKind kind, const TimeGrid& time, int grid, Rng& rng) {
    if (kind == DensityKind::DrumFixed) return;
    const int g = grid * std::max(1, pat.scale);
    std::size_t count = 0;
    std::vector<Step> empty;
    for (std::size_t b = 0; b < pat.bars.size(); ++b) {
        for (int s = 0; s < pat.stepsPerBar; ++s) {
            const char t = pat.bars[b][static_cast<std::size_t>(s)];
            const double jitter = rng.uniform() * 0.01;
            if (onset(t)) {
                ++count;
                continue;
            }
            const Tick at = time.stepOffset(s, g);
            if (depthOf(time, at) >= 3) continue;  // no 32nds or off-grid steps
            const double strength = strengthAt(time, at);
            double score = 0.0;
            switch (kind) {
                case DensityKind::DrumGhost: score = -strength; break;  // ghosts go between the beats
                default: score = strength + (t == '.' ? 0.25 : 0.0); break;
            }
            empty.push_back({static_cast<int>(b), s, score + jitter});
        }
    }
    std::stable_sort(empty.begin(), empty.end(), [](const Step& a, const Step& b) { return a.score > b.score; });
    const auto n = std::min(empty.size(), static_cast<std::size_t>(std::lround(f * static_cast<double>(count))));
    for (std::size_t i = 0; i < n; ++i) {
        const auto& e = empty[i];
        char token = 'x';
        if (kind == DensityKind::DrumGhost) token = 'g';
        else if (kind == DensityKind::Bass && strengthAt(time, time.stepOffset(e.step, g)) < 0.0) token = 'g';
        pat.bars[static_cast<std::size_t>(e.bar)][static_cast<std::size_t>(e.step)] = token;
    }
}

}  // namespace

DensityKind drumDensityKind(DrumVoice voice) {
    switch (voice) {
        case DrumVoice::ClosedHat:
        case DrumVoice::PedalHat:
        case DrumVoice::OpenHat:
        case DrumVoice::Ride:
        case DrumVoice::Shaker:
        case DrumVoice::Tambourine: return DensityKind::DrumTime;
        case DrumVoice::Kick:
        case DrumVoice::Snare:
        case DrumVoice::Clap:
        case DrumVoice::Rim:
        case DrumVoice::Cowbell: return DensityKind::DrumGhost;
        case DrumVoice::LowTom:
        case DrumVoice::MidTom:
        case DrumVoice::HighTom:
        case DrumVoice::Crash:
        case DrumVoice::RideBell: return DensityKind::DrumFixed;
    }
    return DensityKind::DrumFixed;
}

void applyDensity(StepPattern& pattern, double density, DensityKind kind, const TimeGrid& time, int grid,
                  std::uint64_t seed) {
    if (pattern.empty() || density == 0.5) return;
    Rng rng(seed);
    if (density < 0.5) thinPattern(pattern, (0.5 - std::max(0.0, density)) / 0.5, kind, time, grid, rng);
    else fillPattern(pattern, (std::min(1.0, density) - 0.5) / 0.5, kind, time, grid, rng);
}

std::vector<MotifNote> applyDensity(std::vector<MotifNote> notes, double density, const TimeGrid& time,
                                    std::uint64_t seed) {
    if (notes.size() < 2 && density < 0.5) return notes;
    if (notes.empty() || density == 0.5) return notes;
    Rng rng(seed);
    const double eps = 1e-6;
    std::stable_sort(notes.begin(), notes.end(), [](const MotifNote& a, const MotifNote& b) { return a.beat < b.beat; });
    auto strength = [&](double beat) {
        const Tick bar = time.ticksPerBar();
        Tick t = time.beatsToTicks(beat);
        return strengthAt(time, ((t % bar) + bar) % bar);
    };
    if (density < 0.5) {
        const double f = (0.5 - std::max(0.0, density)) / 0.5;
        // The first note always stays; the others go weakest and shortest first.
        std::vector<std::pair<double, std::size_t>> order;
        for (std::size_t i = 0; i < notes.size(); ++i) {
            const double jitter = rng.uniform() * 0.01;
            if (i == 0) continue;
            order.push_back({strength(notes[i].beat) + notes[i].beats + (notes[i].accent ? 0.3 : 0.0) + jitter, i});
        }
        std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        const auto n = static_cast<std::size_t>(std::lround(f * static_cast<double>(order.size())));
        std::vector<bool> gone(notes.size(), false);
        for (std::size_t i = 0; i < n; ++i) gone[order[i].second] = true;
        std::vector<MotifNote> out;
        for (std::size_t i = 0; i < notes.size(); ++i) {
            if (!gone[i]) {
                out.push_back(notes[i]);
                continue;
            }
            // Merged into the note before it when they touch; otherwise a rest.
            if (!out.empty() && std::abs(out.back().beat + out.back().beats - notes[i].beat) < eps)
                out.back().beats += notes[i].beats;
        }
        return out;
    }
    const double f = (std::min(1.0, density) - 0.5) / 0.5;
    std::vector<std::pair<double, std::size_t>> order;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const double jitter = rng.uniform() * 0.01;
        if (notes[i].beats < 0.5 - eps) continue;  // notes under half a beat stay whole
        order.push_back({notes[i].beats + jitter, i});
    }
    std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    const auto n = std::min(order.size(), static_cast<std::size_t>(std::lround(f * static_cast<double>(notes.size()))));
    std::vector<bool> split(notes.size(), false);
    for (std::size_t i = 0; i < n; ++i) split[order[i].second] = true;
    std::vector<MotifNote> out;
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const double coin = rng.uniform();
        if (!split[i]) {
            out.push_back(notes[i]);
            continue;
        }
        MotifNote a = notes[i];
        a.beats = notes[i].beats / 2.0;
        MotifNote b = a;
        b.beat = a.beat + a.beats;
        b.accent = false;
        b.alter = 0;
        // A scale step toward the next note (a neighbour when it repeats).
        int dir = coin < 0.5 ? 1 : -1;
        if (i + 1 < notes.size()) {
            const int here = notes[i].degree + 7 * notes[i].octave, next = notes[i + 1].degree + 7 * notes[i + 1].octave;
            if (next > here) dir = 1;
            else if (next < here) dir = -1;
        }
        b.degree += dir;
        out.push_back(a);
        out.push_back(b);
    }
    return out;
}

}  // namespace flowstate::detail
