#include "flowstate/voicing.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>

namespace flowstate {

namespace {

using Shape = std::vector<int>;

bool isAltered(int t) { return t == 13 || t == 15 || t == 18 || t == 20; }

// Chord tones in priority order for close-family voicings (<= maxVoices).
std::vector<int> coreTones(const Chord& c, int maxVoices) {
    if (c.power) return {0, c.fifth >= 0 ? c.fifth : 7};
    std::vector<int> tensions = c.tensions;
    // Altered tensions first, then the highest named extension (13 > 11 > 9).
    std::stable_sort(tensions.begin(), tensions.end(), [](int a, int b) {
        if (isAltered(a) != isAltered(b)) return isAltered(a);
        return a > b;
    });
    std::vector<int> prio;
    auto push = [&](int iv) {
        if (iv < 0) return;
        for (int p : prio)
            if (mod12(p) == mod12(iv)) return;
        prio.push_back(iv);
    };
    push(c.third);
    push(c.guideSeventh());
    if (c.fifth == 6 || c.fifth == 8) push(c.fifth);
    if (!tensions.empty()) push(tensions[0]);
    push(0);
    for (std::size_t i = 1; i < tensions.size(); ++i) push(tensions[i]);
    push(c.fifth);
    if (c.seventh >= 0 && c.sixth >= 0) push(c.sixth);
    if (static_cast<int>(prio.size()) > maxVoices) prio.resize(static_cast<std::size_t>(maxVoices));
    return prio;
}

// Stacks pitch-class intervals ascending starting from `order[0]`.
Shape stackAscending(const std::vector<int>& order) {
    Shape s;
    for (int iv : order) {
        int v = mod12(iv);
        if (s.empty()) {
            s.push_back(v);
        } else {
            while (v <= s.back()) v += 12;
            s.push_back(v);
        }
    }
    return s;
}

std::vector<Shape> closeShapes(const std::vector<int>& tones) {
    std::vector<int> pcs;
    for (int t : tones) pcs.push_back(mod12(t));
    std::sort(pcs.begin(), pcs.end());
    pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
    std::vector<Shape> shapes;
    for (std::size_t r = 0; r < pcs.size(); ++r) {
        std::vector<int> order;
        for (std::size_t k = 0; k < pcs.size(); ++k) order.push_back(pcs[(r + k) % pcs.size()]);
        shapes.push_back(stackAscending(order));
    }
    return shapes;
}

// Close shapes with exactly four voices (triads double the bottom note).
std::vector<Shape> fourVoiceClose(const Chord& c) {
    auto tones = coreTones(c, 4);
    auto shapes = closeShapes(tones);
    if (tones.size() == 3)
        for (auto& s : shapes) s.push_back(s.front() + 12);
    return shapes;
}

Shape dropVoices(Shape s, std::initializer_list<int> fromTop) {
    const int n = static_cast<int>(s.size());
    for (int k : fromTop) {
        int idx = n - k;
        if (idx >= 0 && idx < n) s[static_cast<std::size_t>(idx)] -= 12;
    }
    std::sort(s.begin(), s.end());
    return s;
}

int chooseNinth(const Chord& c, const Scale& scale) {
    for (int t : c.tensions)
        if (t == 13 || t == 15) return t;
    if (c.hasTension(14)) return 14;
    if (scale.contains(c.root + 2) && c.fifth != 6) return 14;
    if ((c.third == 3 || c.sus) && scale.contains(c.root + 5) && c.third != 5) return 17;
    return 0;
}

int chooseFifthSlot(const Chord& c) {
    if (c.hasTension(21)) return 21;
    if (c.hasTension(20)) return 20;
    if (c.hasTension(18)) return 18;
    if (c.third == 3 && c.hasTension(17)) return 17;
    if (c.fifth >= 0) return c.fifth;
    return 0;
}

std::vector<Shape> rootlessShapes(const Chord& c, const Scale& scale) {
    const int seventh = c.guideSeventh();
    if (seventh < 0 || c.third < 0 || c.power) return {};
    const int ninth = chooseNinth(c, scale);
    const int fifth = chooseFifthSlot(c);
    std::vector<Shape> out;
    out.push_back(stackAscending({c.third, fifth, seventh, ninth}));  // A form
    out.push_back(stackAscending({seventh, ninth, c.third, fifth}));  // B form
    // Remove accidental duplicates (e.g. when the ninth falls back to the root
    // and equals another tone) and degenerate shapes.
    for (auto& s : out) {
        std::set<int> pcs;
        Shape clean;
        for (int v : s)
            if (pcs.insert(mod12(v)).second) clean.push_back(v);
        s = clean;
    }
    return out;
}

std::vector<Shape> shellShapes(const Chord& c) {
    const int third = c.third >= 0 ? c.third : 7;
    const int top = c.guideSeventh() >= 0 ? c.guideSeventh() : (c.fifth >= 0 ? c.fifth : 7);
    std::vector<Shape> out;
    out.push_back(stackAscending({0, third, top}));
    out.push_back(stackAscending({0, top, third}));
    return out;
}

std::vector<Shape> spreadShapes(const Chord& c) {
    const int fifth = c.fifth >= 0 ? c.fifth : 7;
    std::vector<int> upper;
    if (c.third >= 0) upper.push_back(c.third);
    if (c.guideSeventh() >= 0) upper.push_back(c.guideSeventh());
    // Top colour: first named tension, else octave root.
    int colour = -1;
    for (int t : c.tensions)
        if (colour < 0 || isAltered(t)) colour = t;
    upper.push_back(colour >= 0 ? colour : 0);
    if (c.guideSeventh() < 0 && colour < 0) upper.push_back(fifth);
    std::vector<Shape> out;
    for (auto& uc : closeShapes(upper)) {
        Shape s{0, fifth};
        int base = 0;
        while (uc.front() + base <= fifth + 2) base += 12;
        for (int v : uc) s.push_back(v + base);
        if (s.back() - s.front() >= 16) out.push_back(s);
    }
    return out;
}

std::vector<Shape> quartalShapes(const Chord& c, const Scale& scale) {
    std::set<int> allowed;
    for (int iv : c.intervals()) allowed.insert(mod12(iv));
    if (!c.hasTension(13) && !c.hasTension(15) && scale.contains(c.root + 2)) allowed.insert(2);
    if (c.third != 4 && !c.hasTension(18) && scale.contains(c.root + 5)) allowed.insert(5);
    if (!c.hasTension(20) && c.fifth != 8 && scale.contains(c.root + 9)) allowed.insert(9);
    auto ok = [&](int iv) { return allowed.count(mod12(iv)) > 0; };
    const int guide3 = c.third;
    const int guide7 = c.guideSeventh();
    std::vector<Shape> out;
    for (int start : allowed) {
        Shape s{start};
        bool fail = false;
        int tritones = 0;
        for (int k = 0; k < 3 && !fail; ++k) {
            int cur = s.back();
            if (ok(cur + 5)) s.push_back(cur + 5);
            else if (ok(cur + 6) && tritones == 0) {
                s.push_back(cur + 6);
                ++tritones;
            } else fail = true;
        }
        if (fail) continue;
        bool hasGuide = false;
        for (int v : s)
            if ((guide3 >= 0 && mod12(v) == mod12(guide3)) || (guide7 >= 0 && mod12(v) == mod12(guide7)))
                hasGuide = true;
        if (!hasGuide) continue;
        out.push_back(s);
        // "So What" shape: three fourths plus a major third on top.
        Shape sw(s.begin(), s.begin() + 3);
        if (ok(sw.back() + 4)) {
            sw.push_back(sw.back() + 4);
            out.push_back(sw);
        }
    }
    return out;
}

std::vector<Shape> powerShapes(const Chord& c) {
    const int fifth = c.fifth >= 0 ? c.fifth : 7;
    return {{0, fifth}, {0, fifth, 12}};
}

std::vector<Voicing> place(const std::vector<Shape>& shapes, int root, int low, int high) {
    std::vector<Voicing> out;
    std::set<Voicing> seen;
    for (const auto& s : shapes) {
        if (s.empty()) continue;
        for (int r = mod12(root) - 48; r <= high; r += 12) {
            if (r + s.front() < low) continue;
            if (r + s.back() > high) break;
            Voicing v;
            for (int iv : s) v.push_back(r + iv);
            if (v.front() < 0 || v.back() > 127) continue;
            if (seen.insert(v).second) out.push_back(v);
        }
    }
    return out;
}

Voicing foldIntoRange(const std::vector<int>& tones, int root, int low, int high) {
    std::set<int> pitches;
    const int centre = (low + high) / 2;
    for (int iv : tones) {
        int pc = mod12(root + iv);
        int best = -1;
        for (int p = pc; p <= 127; p += 12) {
            if (p < low || p > high) continue;
            if (best < 0 || std::abs(p - centre) < std::abs(best - centre)) best = p;
        }
        if (best < 0) best = std::clamp(centre - mod12(centre - pc), 0, 127);
        pitches.insert(best);
    }
    return Voicing(pitches.begin(), pitches.end());
}

double lowIntervalPenalty(const Voicing& v) {
    double p = 0.0;
    for (std::size_t i = 1; i < v.size(); ++i) {
        int a = v[i - 1];
        int iv = v[i] - a;
        if (a < 40 && iv < 7) p += 8.0;
        else if (a < 48 && iv < 5) p += 5.0;
        else if (a < 55 && iv < 3) p += 3.0;
    }
    return p;
}

double staticCost(const Voicing& v, int low, int high) {
    double mean = 0.0;
    for (int p : v) mean += p;
    mean /= static_cast<double>(v.size());
    const double centre = (low + high) / 2.0;
    return std::abs(mean - centre) * 0.3 + lowIntervalPenalty(v);
}

double transitionCost(const Voicing& a, const Voicing& b) {
    double cost = voiceMovement(a, b);
    int top = std::abs(a.back() - b.back());
    cost += 0.5 * top;
    if (top > 5) cost += (top - 5);
    cost += 0.25 * std::abs(a.front() - b.front());
    return cost;
}

// Places the slash bass under a voicing if the range allows.
bool addSlashBass(Voicing& v, int bassPc, int rootPc, VoicingFamily family, int low) {
    const bool rootBottomFamily =
        family == VoicingFamily::Shell || family == VoicingFamily::Spread || family == VoicingFamily::Power;
    Voicing base = v;
    if (rootBottomFamily && mod12(base.front()) == mod12(rootPc) && base.size() > 1) base.erase(base.begin());
    int b = base.front() - 1;
    while (b >= low && mod12(b) != bassPc) --b;
    if (b < low) return false;
    // Remove duplicates of the bass pitch class directly above (keeps the colour).
    base.insert(base.begin(), b);
    v = base;
    return true;
}

}  // namespace

std::vector<std::vector<int>> voicingShapes(const Chord& chord, VoicingFamily family, const Scale& scale) {
    switch (family) {
        case VoicingFamily::Close: return closeShapes(coreTones(chord, 4));
        case VoicingFamily::Drop2: {
            std::vector<Shape> out;
            for (auto& s : fourVoiceClose(chord)) out.push_back(dropVoices(s, {2}));
            return out;
        }
        case VoicingFamily::Drop3: {
            std::vector<Shape> out;
            for (auto& s : fourVoiceClose(chord)) out.push_back(dropVoices(s, {3}));
            return out;
        }
        case VoicingFamily::Open: {
            auto tones = coreTones(chord, 4);
            std::vector<Shape> out;
            for (auto& s : closeShapes(tones))
                out.push_back(s.size() >= 4 ? dropVoices(s, {2, 4}) : dropVoices(s, {2}));
            return out;
        }
        case VoicingFamily::Rootless: return rootlessShapes(chord, scale);
        case VoicingFamily::Shell: return shellShapes(chord);
        case VoicingFamily::Spread: return spreadShapes(chord);
        case VoicingFamily::Quartal: return quartalShapes(chord, scale);
        case VoicingFamily::Power: return powerShapes(chord);
    }
    return {};
}

std::vector<Voicing> voicingCandidates(const Chord& chord, VoicingFamily family, int low, int high,
                                       const Scale& scale, bool* usedFallback) {
    if (usedFallback) *usedFallback = false;
    auto shapes = voicingShapes(chord, family, scale);
    auto out = place(shapes, chord.root, low, high);
    if (out.empty() && family == VoicingFamily::Spread) {
        if (usedFallback) *usedFallback = true;
        out = place(voicingShapes(chord, VoicingFamily::Open, scale), chord.root, low, high);
    }
    if (out.empty() && family != VoicingFamily::Close) {
        if (usedFallback) *usedFallback = true;
        out = place(voicingShapes(chord, VoicingFamily::Close, scale), chord.root, low, high);
    }
    if (out.empty()) {
        if (usedFallback) *usedFallback = true;
        // Narrow range: shrink to fewer voices, then fold.
        auto tones = coreTones(chord, 3);
        out = place(closeShapes(tones), chord.root, low, high);
        if (out.empty()) out.push_back(foldIntoRange(tones, chord.root, low, high));
    }
    return out;
}

int voiceMovement(const Voicing& a, const Voicing& b) {
    if (a.empty() || b.empty()) return 0;
    if (a.size() == b.size()) {
        int sum = 0;
        for (std::size_t i = 0; i < a.size(); ++i) sum += std::abs(a[i] - b[i]);
        return sum;
    }
    auto nearest = [](int p, const Voicing& v) {
        int best = std::numeric_limits<int>::max();
        for (int q : v) best = std::min(best, std::abs(p - q));
        return best;
    };
    int sum = 0;
    for (int p : a) sum += nearest(p, b);
    for (int p : b) sum += nearest(p, a);
    return (sum + 1) / 2;
}

std::vector<Voicing> voiceLead(const std::vector<VoicingStep>& steps, int low, int high, const Scale& scale,
                               std::vector<std::string>* warnings) {
    std::vector<std::vector<Voicing>> cands;
    std::vector<std::vector<double>> stat;
    std::set<std::string> fallbackWarned;
    for (const auto& step : steps) {
        bool fallback = false;
        auto vs = voicingCandidates(step.chord, step.family, low, high, scale, &fallback);
        if (fallback && warnings && fallbackWarned.insert(step.chord.symbol).second)
            warnings->push_back(std::string("voicing: \"") + toString(step.family) + "\" cannot voice \"" +
                                step.chord.symbol + "\" in range; used a fallback voicing");
        std::vector<double> sc;
        const bool wantsBass = step.includeSlashBass && step.chord.bass >= 0;
        for (auto& v : vs) {
            double cost = 0.0;
            if (wantsBass && !addSlashBass(v, step.chord.bass, step.chord.root, step.family, low)) cost += 6.0;
            cost += staticCost(v, low, high);
            sc.push_back(cost);
        }
        cands.push_back(std::move(vs));
        stat.push_back(std::move(sc));
    }
    const std::size_t n = steps.size();
    std::vector<Voicing> result(n);
    if (n == 0) return result;
    // First chord: stronger pull to the centre of the range.
    std::vector<std::vector<double>> cost(n);
    std::vector<std::vector<int>> back(n);
    for (std::size_t j = 0; j < cands[0].size(); ++j) cost[0].push_back(stat[0][j] * 2.0);
    back[0].assign(cands[0].size(), -1);
    for (std::size_t i = 1; i < n; ++i) {
        cost[i].assign(cands[i].size(), std::numeric_limits<double>::infinity());
        back[i].assign(cands[i].size(), 0);
        for (std::size_t j = 0; j < cands[i].size(); ++j) {
            for (std::size_t k = 0; k < cands[i - 1].size(); ++k) {
                double c = cost[i - 1][k] + transitionCost(cands[i - 1][k], cands[i][j]) + stat[i][j];
                if (c < cost[i][j] - 1e-9) {
                    cost[i][j] = c;
                    back[i][j] = static_cast<int>(k);
                }
            }
        }
    }
    std::size_t best = 0;
    for (std::size_t j = 1; j < cands[n - 1].size(); ++j)
        if (cost[n - 1][j] < cost[n - 1][best] - 1e-9) best = j;
    for (std::size_t i = n; i-- > 0;) {
        result[i] = cands[i][best];
        if (i > 0) best = static_cast<std::size_t>(back[i][best]);
    }
    return result;
}

}  // namespace flowstate
