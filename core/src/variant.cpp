// Re-roll variants (P1-22): seeded choices a part makes when it has its own seed. The score's
// musical choices stay (chords, motifs, rhythm, groove); the take changes: small rhythm moves per
// bar, passing tones in the bass, ghost notes and pushes in the drums, varied motif repeats.
// A part without a seed never comes here, so it realizes exactly as before.
#include "realize_internal.h"

#include <algorithm>
#include <cmath>

namespace flowstate::detail {

namespace {

// How likely a bar, or a motif statement, is to get a move.
constexpr double kBarChance = 0.45;
constexpr double kMotifChance = 0.7;

double strengthOf(const TimeGrid& time, int step, int grid) {
    const Tick at = time.stepOffset(step, grid);
    const Tick beat = time.ticksPerBeat();
    int depth = 0;
    if (at % beat != 0) depth = (at * 2) % beat == 0 ? 1 : (at * 4) % beat == 0 ? 2 : 3;
    return metricWeight(time, at) - 0.1 * depth;
}

bool onset(char t) { return t != '.' && t != '-'; }

// A weak onset moves one step earlier into a rest before it: a push. Its holds now follow from it.
bool push(std::string& bar, const TimeGrid& time, int grid, Rng& rng) {
    std::vector<int> can;
    for (int s = 1; s < static_cast<int>(bar.size()); ++s)
        if (onset(bar[static_cast<std::size_t>(s)]) && bar[static_cast<std::size_t>(s - 1)] == '.' &&
            strengthOf(time, s, grid) < 1.0 && bar[static_cast<std::size_t>(s)] != 'X')
            can.push_back(s);
    if (can.empty()) return false;
    const int s = can[static_cast<std::size_t>(rng.below(static_cast<int>(can.size())))];
    bar[static_cast<std::size_t>(s - 1)] = bar[static_cast<std::size_t>(s)];
    bar[static_cast<std::size_t>(s)] = '-';
    return true;
}

// A token added on an empty weak step (an eighth or sixteenth off the beat).
bool add(std::string& bar, char token, const TimeGrid& time, int grid, Rng& rng) {
    std::vector<int> can;
    for (int s = 0; s < static_cast<int>(bar.size()); ++s) {
        const char t = bar[static_cast<std::size_t>(s)];
        const double w = strengthOf(time, s, grid);
        if ((t == '.' || t == '-') && w < 0.0 && w > -0.75) can.push_back(s);
    }
    if (can.empty()) return false;
    bar[static_cast<std::size_t>(can[static_cast<std::size_t>(rng.below(static_cast<int>(can.size())))])] = token;
    return true;
}

// A weak onset dropped (drums: one hit of a busy lane).
bool drop(std::string& bar, const TimeGrid& time, int grid, Rng& rng) {
    std::vector<int> can;
    int count = 0;
    for (int s = 0; s < static_cast<int>(bar.size()); ++s) {
        const char t = bar[static_cast<std::size_t>(s)];
        if (!onset(t)) continue;
        ++count;
        if (t != 'X' && strengthOf(time, s, grid) < 0.0) can.push_back(s);
    }
    if (can.empty() || count < 4) return false;
    bar[static_cast<std::size_t>(can[static_cast<std::size_t>(rng.below(static_cast<int>(can.size())))])] = '.';
    return true;
}

// Bass: a root after the downbeat becomes the fifth or the octave.
bool passing(std::string& bar, const TimeGrid& time, int grid, Rng& rng) {
    std::vector<int> can;
    for (int s = 0; s < static_cast<int>(bar.size()); ++s) {
        const char t = bar[static_cast<std::size_t>(s)];
        if ((t == 'x' || t == 'R' || t == '1') && strengthOf(time, s, grid) < 1.0) can.push_back(s);
    }
    if (can.empty()) return false;
    const int s = can[static_cast<std::size_t>(rng.below(static_cast<int>(can.size())))];
    bar[static_cast<std::size_t>(s)] = rng.uniform() < 0.5 ? '5' : '8';
    return true;
}

// A held note is struck again on a beat or an eighth inside its hold (chords: a re-strike; bass: a
// fifth, octave or root).
bool split(std::string& bar, const char* tokens, const TimeGrid& time, int grid, Rng& rng) {
    std::vector<int> can;
    for (int s = 1; s < static_cast<int>(bar.size()); ++s)
        if (bar[static_cast<std::size_t>(s)] == '-' && strengthOf(time, s, grid) >= -0.15) can.push_back(s);
    if (can.empty()) return false;
    const int s = can[static_cast<std::size_t>(rng.below(static_cast<int>(can.size())))];
    const std::string options(tokens);
    bar[static_cast<std::size_t>(s)] = options[static_cast<std::size_t>(rng.below(static_cast<int>(options.size())))];
    return true;
}

// Bass: the last eighth of the bar becomes a chromatic approach into the next chord's root.
bool approach(std::string& bar, const TimeGrid& time, int grid) {
    for (int s = static_cast<int>(bar.size()) - 1; s > 0; --s) {
        if (strengthOf(time, s, grid) < -0.15) continue;  // the last eighth or beat
        const char t = bar[static_cast<std::size_t>(s)];
        if (t == 'a' || strengthOf(time, s, grid) >= 1.0) return false;
        bar[static_cast<std::size_t>(s)] = 'a';
        return true;
    }
    return false;
}

}  // namespace

void varyPattern(StepPattern& pat, int blockBars, DensityKind kind, const TimeGrid& time, int grid, Rng& rng) {
    if (pat.empty() || kind == DensityKind::DrumFixed) return;
    const int g = grid * std::max(1, pat.scale);
    // Each bar of the block varies on its own, so the pattern is written out bar by bar.
    const int bars = std::clamp(blockBars, 1, 64);
    std::vector<std::string> out;
    for (int b = 0; b < bars; ++b) out.push_back(pat.bars[static_cast<std::size_t>(b) % pat.bars.size()]);
    for (auto& bar : out) {
        const double chance = rng.uniform();
        const double which = rng.uniform();
        if (chance >= kBarChance) continue;
        switch (kind) {
            case DensityKind::Chords:
                if (which < 0.5) push(bar, time, g, rng) || split(bar, "x", time, g, rng);
                else split(bar, "x", time, g, rng) || push(bar, time, g, rng);
                break;
            case DensityKind::Line: push(bar, time, g, rng); break;
            case DensityKind::Bass:
                if (which < 0.3) approach(bar, time, g) || split(bar, "58R", time, g, rng);
                else if (which < 0.6) split(bar, "58R", time, g, rng) || passing(bar, time, g, rng);
                else if (which < 0.8) passing(bar, time, g, rng) || push(bar, time, g, rng);
                else push(bar, time, g, rng) || approach(bar, time, g);
                break;
            case DensityKind::DrumTime:
                if (which < 0.5) add(bar, 'g', time, g, rng) || drop(bar, time, g, rng);
                else drop(bar, time, g, rng) || add(bar, 'g', time, g, rng);
                break;
            case DensityKind::DrumGhost:
                if (which < 0.5) add(bar, 'g', time, g, rng) || push(bar, time, g, rng);
                else push(bar, time, g, rng) || add(bar, 'g', time, g, rng);
                break;
            case DensityKind::DrumFixed: break;
        }
    }
    pat.bars = std::move(out);
}

std::vector<MotifNote> varyMotif(std::vector<MotifNote> notes, Rng& rng) {
    const double chance = rng.uniform();
    const double which = rng.uniform();
    if (notes.size() < 2 || chance >= kMotifChance) return notes;
    // Never the first note or an accent, so the motif stays recognisable.
    std::vector<std::size_t> can;
    for (std::size_t i = 1; i < notes.size(); ++i)
        if (!notes[i].accent && notes[i].beats <= 1.0) can.push_back(i);
    if (can.empty()) return notes;
    const std::size_t i = can[static_cast<std::size_t>(rng.below(static_cast<int>(can.size())))];
    if (which < 0.6 || i + 1 >= notes.size() || notes[i + 1].accent ||
        std::abs(notes[i].beat + notes[i].beats - notes[i + 1].beat) > 1e-6 || notes[i].beats == notes[i + 1].beats) {
        // A scale step up or down: a neighbour or passing tone.
        notes[i].degree += rng.uniform() < 0.5 ? 1 : -1;
    } else {
        // Two touching notes swap lengths: a long-short figure becomes short-long.
        std::swap(notes[i].beats, notes[i + 1].beats);
        notes[i + 1].beat = notes[i].beat + notes[i].beats;
    }
    return notes;
}

}  // namespace flowstate::detail
