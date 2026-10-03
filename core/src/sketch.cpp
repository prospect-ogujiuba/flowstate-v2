// Instant sketch (P1-10): rule-based IR from the session context. See sketch.h.
#include "flowstate/sketch.h"

#include "flowstate/analyze.h"
#include "flowstate/groove.h"
#include "flowstate/grooves.h"
#include "flowstate/theory.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace flowstate {

namespace {

using nlohmann::json;

bool hasTag(const Context& ctx, std::initializer_list<const char*> tags) {
    for (const auto& s : ctx.style)
        for (const char* t : tags)
            if (s.find(t) != std::string::npos) return true;
    return false;
}

// The mode the progression is built in: pentatonic and blues scales borrow their parent's chords.
Mode chordMode(Mode m) {
    if (m == Mode::MajorPentatonic) return Mode::Major;
    if (m == Mode::MinorPentatonic || m == Mode::Blues) return Mode::Minor;
    return m;
}

// Progressions as scale degrees, four chords that loop.
const std::vector<std::vector<int>>& progressions(Mode m) {
    static const std::vector<std::vector<int>> major{{1, 5, 6, 4}, {1, 6, 4, 5}, {6, 4, 1, 5}, {2, 5, 1, 1}, {1, 4, 6, 5}};
    static const std::vector<std::vector<int>> minor{{1, 6, 3, 7}, {1, 4, 7, 3}, {1, 6, 4, 5}, {1, 7, 6, 7}, {1, 4, 5, 1}};
    static const std::vector<std::vector<int>> dorian{{1, 4, 1, 4}, {1, 2, 4, 1}, {1, 7, 4, 1}};
    static const std::vector<std::vector<int>> mixolydian{{1, 7, 4, 1}, {1, 4, 7, 4}};
    static const std::vector<std::vector<int>> phrygian{{1, 2, 1, 7}, {1, 2, 3, 2}};
    static const std::vector<std::vector<int>> lydian{{1, 2, 1, 7}, {1, 2, 5, 1}};
    switch (m) {
        case Mode::Dorian: return dorian;
        case Mode::Mixolydian: return mixolydian;
        case Mode::Phrygian:
        case Mode::Locrian: return phrygian;
        case Mode::Lydian: return lydian;
        case Mode::Minor:
        case Mode::HarmonicMinor:
        case Mode::MelodicMinor: return minor;
        default: return major;
    }
}

// Flat spellings in flat keys (relative to the parent major scale).
bool prefersFlats(const Context& ctx) {
    if (ctx.tonic.find('#') != std::string::npos) return false;
    if (ctx.tonic.size() > 1 && ctx.tonic[1] == 'b') return true;
    int offset = 0;
    switch (ctx.mode) {
        case Mode::Dorian: offset = 2; break;
        case Mode::Phrygian: offset = 4; break;
        case Mode::Lydian: offset = 5; break;
        case Mode::Mixolydian: offset = 7; break;
        case Mode::Minor:
        case Mode::HarmonicMinor:
        case Mode::MelodicMinor:
        case Mode::MinorPentatonic:
        case Mode::Blues: offset = 9; break;
        case Mode::Locrian: offset = 11; break;
        default: break;
    }
    const int parent = mod12(pitchClassFromName(ctx.tonic) - offset);
    return parent == 5 || parent == 10 || parent == 3 || parent == 8 || parent == 1 || parent == 6;
}

std::string chordSymbol(const Scale& scale, int degree, bool seventh, bool dominant, bool flats) {
    Chord c = diatonicChord(scale, degree, seventh);
    std::vector<int> pitches;
    pitches.reserve(4);
    pitches.push_back(48 + c.root);
    if (dominant) {
        pitches.push_back(48 + c.root + 4);
        pitches.push_back(48 + c.root + 7);
        pitches.push_back(48 + c.root + 10);
    } else {
        if (c.third >= 0) pitches.push_back(48 + c.root + c.third);
        if (c.fifth >= 0) pitches.push_back(48 + c.root + c.fifth);
        if (seventh && c.seventh >= 0) pitches.push_back(48 + c.root + c.seventh);
    }
    const std::string name = nameChord(pitches, flats);
    return name.empty() ? c.symbol : name;
}

std::string repeatCell(const std::string& cell, int beats) {
    std::string s;
    for (int i = 0; i < beats; ++i) s += cell;
    return s;
}

// A held note: the first step, then holds to the end of `beats` beats.
std::string held(char first, int beats, int grid) {
    std::string s(static_cast<std::size_t>(beats * grid), '-');
    s[0] = first;
    return s;
}

// The beat where a bar's "middle" accent falls: 3 in 4/4, 4 in 6/8 (3+3), 3 in 5/4 (2+3) and 7/8 (2+2+3).
int middleBeat(int n) {
    if (n % 3 == 0 && n > 3) return n / 2;
    if (n == 5 || n == 7) return 2;
    return n / 2;
}

std::string chordsRhythm(int n, int grid, int pick) {
    switch (pick % 4) {
        case 0: return held('x', n, grid);
        case 1: return repeatCell(held('x', 1, grid), n);  // a hit on every beat
        case 2: {
            // Pushed: hit on 1, again just before the middle, held through.
            std::string s(static_cast<std::size_t>(n * grid), '-');
            s[0] = 'x';
            const int push = middleBeat(n) * grid - 1;
            if (push > 0) s[static_cast<std::size_t>(push)] = 'x';
            return s;
        }
        default: {
            // Off-beat stabs (one per beat, on the half-beat).
            std::string cell(static_cast<std::size_t>(grid), '.');
            cell[static_cast<std::size_t>(grid / 2)] = 'x';
            return repeatCell(cell, n);
        }
    }
}

std::string bassRhythm(int n, int grid, int pick) {
    const std::size_t len = static_cast<std::size_t>(n * grid);
    std::string s(len, '.');
    const auto at = [&](int beat, int step, char c) {
        const int i = beat * grid + step;
        if (i >= 0 && static_cast<std::size_t>(i) < len) s[static_cast<std::size_t>(i)] = c;
    };
    const int mid = middleBeat(n);
    switch (pick % 4) {
        case 0:  // root held, fifth on the middle, approach into the next chord
            s = std::string(len, '-');
            s[0] = 'R';
            at(mid, 0, '5');
            if (grid >= 2) at(n - 1, grid - 1, 'a');
            break;
        case 1:  // root on every beat
            for (int b = 0; b < n; ++b) at(b, 0, b == 0 ? 'R' : '1');
            break;
        case 2:  // octave pump on the off-beats
            for (int b = 0; b < n; ++b) {
                at(b, 0, 'R');
                if (grid >= 2) at(b, grid / 2, '8');
            }
            break;
        default:  // syncopated: root, pushed root before the middle, fifth after it
            at(0, 0, 'R');
            if (grid >= 2) at(mid - 1, grid / 2, 'R');
            at(mid, grid >= 2 ? grid / 2 : 0, '5');
            break;
    }
    return s;
}

// Melody: onsets drawn from the seed, denser on strong beats, with room to breathe. With a rhythm and no
// motif, the realizer draws a stepwise line over chord tones (ir-spec, "Step tokens").
std::string melodyBar(int n, int grid, Rng& rng, bool last, double density) {
    const std::size_t len = static_cast<std::size_t>(n * grid);
    std::string s(len, '.');
    for (std::size_t i = 0; i < len; ++i) {
        const bool onBeat = i % static_cast<std::size_t>(grid) == 0;
        const double p = (onBeat ? 0.55 : 0.3) * density * 2.0;
        if (rng.uniform() < p) s[i] = 'x';
        else if (i > 0 && s[i - 1] != '.' && rng.uniform() < 0.5) s[i] = '-';
    }
    if (s.find('x') == std::string::npos) s[0] = 'x';
    if (last) {
        // End the phrase on a held note from the middle of the last bar.
        const std::size_t from = static_cast<std::size_t>(middleBeat(n) * grid);
        for (std::size_t i = from; i < len; ++i) s[i] = i == from ? 'x' : '-';
    }
    return s;
}

std::string grooveFor(const Context& ctx, Rng& rng) {
    const int n = ctx.meterNumerator, d = ctx.meterDenominator;
    struct Tag { const char* tag; const char* groove; };
    static const Tag byStyle[] = {
        {"trap", "trap"}, {"drill", "drill"}, {"reggaeton", "dembow"}, {"dembow", "dembow"}, {"afro", "afrobeats"},
        {"house", "four_on_floor"}, {"techno", "tech_house"}, {"edm", "four_on_floor"}, {"disco", "four_on_floor"},
        {"lofi", "lofi"}, {"lo-fi", "lofi"}, {"boom", "boom_bap"}, {"hip-hop", "boom_bap"}, {"hip hop", "boom_bap"},
        {"funk", "funk"}, {"rock", "rock"}, {"pop", "pop"}, {"ballad", "ballad"}, {"neo-soul", "neo_soul"},
        {"neo soul", "neo_soul"}, {"r&b", "neo_soul"}, {"rnb", "neo_soul"}, {"soul", "neo_soul"},
        {"dnb", "dnb"}, {"drum and bass", "dnb"}, {"jungle", "dnb"}, {"cinematic", "cinematic_toms"},
        {"ambient", "sparse_pulse"}, {"jazz", "jazz_swing"}, {"gospel", "gospel_shuffle"}, {"waltz", "waltz"},
    };
    const auto fits = [&](const char* name) {
        const Groove* g = findGroove(name);
        return g != nullptr && g->numerator == n && g->denominator == d;
    };
    for (const auto& t : byStyle)
        if (hasTag(ctx, {t.tag}) && fits(t.groove)) return t.groove;
    // Jazz in 3/4 is a jazz waltz.
    if (hasTag(ctx, {"jazz"}) && fits("jazz_waltz")) return "jazz_waltz";
    // No style match: what fits the meter, picked by the seed (and the tempo, in 4/4).
    std::vector<std::string> fitting;
    if (n == 4 && d == 4) {
        if (ctx.tempo >= 118) fitting = {"four_on_floor", "pop", "dnb"};
        else if (ctx.tempo >= 90) fitting = {"pop", "boom_bap", "funk", "rock"};
        else fitting = {"boom_bap", "lofi", "half_time", "ballad"};
        if (ctx.tempo >= 130) fitting.push_back("trap");
        if (ctx.tempo >= 160) fitting = {"dnb", "trap"};
    } else {
        for (const auto& g : grooves())
            if (g.numerator == n && g.denominator == d) fitting.push_back(g.name);
    }
    if (fitting.empty()) return "";
    return fitting[static_cast<std::size_t>(rng.below(static_cast<int>(fitting.size())))];
}

// Lanes for a meter no groove covers: kick on 1, snare on the middle, a hat on every beat.
json laneDrums(int n, int grid) {
    std::string kick(static_cast<std::size_t>(n * grid), '.'), snare = kick, hat = kick;
    kick[0] = 'X';
    snare[static_cast<std::size_t>(middleBeat(n) * grid)] = 'x';
    for (int b = 0; b < n; ++b) hat[static_cast<std::size_t>(b * grid)] = b == 0 ? 'X' : 'x';
    return json::array({{{"voice", "kick"}, {"steps", kick}}, {{"voice", "snare"}, {"steps", snare}}, {{"voice", "closed_hat"}, {"steps", hat}}});
}

const char* roleName(Role r) { return toString(r); }

}  // namespace

std::string sketchJson(const SketchRequest& req) {
    const Context& ctx = req.context;
    const int n = std::max(1, ctx.meterNumerator);
    const int bars = std::clamp(ctx.bars, 1, 64);
    Rng rng(mixSeed(req.seed, "sketch"));
    const auto pick = [&](int count) { return rng.below(count); };

    // ---- Harmony: a looping four-chord progression, one chord a bar (two a bar in short clips) ----
    const Mode cm = chordMode(ctx.mode);
    const Scale scale = makeScale(pitchClassFromName(ctx.tonic), cm);
    const auto& progs = progressions(cm);
    const auto& prog = progs[static_cast<std::size_t>(pick(static_cast<int>(progs.size())))];
    const bool colour = hasTag(ctx, {"jazz", "neo", "soul", "lofi", "lo-fi", "r&b", "rnb", "gospel", "house"}) || pick(3) == 0;
    const bool blues = ctx.mode == Mode::Blues;
    const bool flats = prefersFlats(ctx);
    const int chordsPerBar = bars < 4 && n % 2 == 0 ? 2 : 1;
    const int chordBeats = n / chordsPerBar;
    std::string harmony;
    for (int i = 0; i < bars * chordsPerBar; ++i) {
        const int degree = blues ? std::vector<int>{1, 4, 1, 5}[static_cast<std::size_t>(i % 4)] : prog[static_cast<std::size_t>(i % prog.size())];
        const int beats = chordsPerBar == 1 ? n : (i % 2 == 0 ? chordBeats : n - chordBeats);
        if (!harmony.empty()) harmony += i % chordsPerBar == 0 ? " | " : " ";
        harmony += chordSymbol(scale, degree, colour || blues, blues, flats) + ":" + std::to_string(beats);
    }

    // ---- Form: one section, or two that lift in longer clips ----
    json form = json::array();
    if (bars >= 8) {
        const int a = bars / 2;
        form.push_back({{"name", "A"}, {"startBar", 1}, {"bars", a}, {"energy", 0.55}});
        form.push_back({{"name", "B"}, {"startBar", a + 1}, {"bars", bars - a}, {"energy", 0.72}});
    } else {
        form.push_back({{"name", "A"}, {"startBar", 1}, {"bars", bars}, {"energy", 0.62}});
    }

    // ---- Parts ----
    std::vector<Role> roles = req.roles;
    if (roles.empty()) roles = {Role::Chords, Role::Bass, Role::Melody, Role::Drums};
    const int grid = 2;
    const std::string groove = grooveFor(ctx, rng);
    double swing = ctx.swing;
    if (swing == 0.0 && (groove == "lofi" || groove == "boom_bap" || groove == "neo_soul")) swing = 0.12;
    const std::vector<std::string> chordVoicings =
        hasTag(ctx, {"jazz", "neo", "soul", "r&b", "rnb", "lofi", "lo-fi"}) ? std::vector<std::string>{"drop2", "rootless", "shell"}
        : hasTag(ctx, {"rock", "punk", "metal"})                             ? std::vector<std::string>{"power", "close"}
                                                                             : std::vector<std::string>{"close", "open", "drop2", "spread"};

    json parts = json::array();
    std::vector<std::string> used;
    for (const Role role : roles) {
        const std::string name = roleName(role);
        if (std::find(used.begin(), used.end(), name) != used.end()) continue;
        used.push_back(name);
        json part{{"id", "sketch-" + name}, {"role", name}, {"grid", grid}};
        json block{{"startBar", 1}, {"endBar", bars}};
        switch (role) {
            case Role::Chords:
                part["name"] = "Keys";
                part["low"] = "C3"; part["high"] = "G5"; part["velocity"] = 78;
                block["rhythm"] = chordsRhythm(n, grid, pick(4));
                block["voicing"] = chordVoicings[static_cast<std::size_t>(pick(static_cast<int>(chordVoicings.size())))];
                break;
            case Role::Pad:
                part["name"] = "Pad";
                part["low"] = "C3"; part["high"] = "C5"; part["velocity"] = 64;
                block["rhythm"] = held('x', n, grid);
                block["voicing"] = "open";
                block["articulation"] = "legato";
                break;
            case Role::Arp: {
                part["name"] = "Arp";
                part["low"] = "C4"; part["high"] = "C6"; part["velocity"] = 70;
                part["grid"] = 4;
                block["rhythm"] = repeatCell("x", n * 4);
                static const char* patterns[] = {"up", "updown", "chord_tones", "down"};
                block["arpPattern"] = patterns[pick(4)];
                break;
            }
            case Role::Bass:
                part["name"] = "Bass";
                part["low"] = "E1"; part["high"] = "C3"; part["velocity"] = 96;
                block["rhythm"] = bassRhythm(n, grid, pick(4));
                break;
            case Role::Melody:
            case Role::Counter: {
                const bool counter = role == Role::Counter;
                part["name"] = counter ? "Counter" : "Lead";
                part["low"] = counter ? "G3" : "C4"; part["high"] = counter ? "C5" : "A5";
                part["velocity"] = counter ? 74 : 92;
                // A two-bar phrase that repeats, ending on a held note.
                Rng line(mixSeed(req.seed, "sketch:" + name));
                const double density = counter ? 0.3 : 0.45 + 0.1 * line.below(3);
                const std::string one = melodyBar(n, grid, line, false, density);
                const std::string two = melodyBar(n, grid, line, true, density);
                block["rhythm"] = bars == 1 ? two : one + "|" + two;
                break;
            }
            case Role::Drums:
                part["name"] = "Drums";
                part["low"] = "C1"; part["high"] = "C6"; part["velocity"] = 96;
                if (!groove.empty()) block["groove"] = groove;
                else block["drums"] = laneDrums(n, grid);
                if (bars >= 4) block["fill"] = std::vector<const char*>{"snare_roll", "kick_build", "none"}[static_cast<std::size_t>(pick(3))];
                break;
        }
        part["blocks"] = json::array({block});
        parts.push_back(std::move(part));
    }

    json score{
        {"ir", kIrId},
        {"title", "Sketch"},
        {"context",
         {{"tempo", ctx.tempo}, {"meterNumerator", n}, {"meterDenominator", ctx.meterDenominator}, {"tonic", ctx.tonic},
          {"mode", toString(ctx.mode)}, {"bars", bars}, {"swing", swing}, {"style", ctx.style}}},
        {"form", form},
        {"harmony", harmony},
        {"motifs", json::array()},
        {"parts", parts},
    };
    return score.dump();
}

}  // namespace flowstate
