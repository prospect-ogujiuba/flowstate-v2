// Local transforms (P1-21): JSON in, JSON out, so every field the transform doesn't touch (the
// compact harmony and motif strings, block order, unknown keys) stays exactly as written.
#include "flowstate/transform.h"

#include "flowstate/ir.h"
#include "flowstate/realize.h"
#include "flowstate/theory.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <set>

#include <nlohmann/json.hpp>

namespace flowstate {

using nlohmann::json;

namespace {

constexpr double kDensityStep = 0.25;

const char* const kSharpNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
const char* const kFlatNames[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

std::string pcName(int pc, bool flats) { return (flats ? kFlatNames : kSharpNames)[mod12(pc)]; }

// Semitones from the mode's tonic down to its relative major's tonic (the key signature).
int relativeMajorOffset(Mode mode) {
    switch (mode) {
        case Mode::Major:
        case Mode::MajorPentatonic: return 0;
        case Mode::Dorian: return 2;
        case Mode::Phrygian: return 4;
        case Mode::Lydian: return 5;
        case Mode::Mixolydian: return 7;
        case Mode::Locrian: return 11;
        case Mode::Minor:
        case Mode::HarmonicMinor:
        case Mode::MelodicMinor:
        case Mode::MinorPentatonic:
        case Mode::Blues: return 9;
    }
    return 0;
}

// Flat keys spell with flats: F, Bb, Eb, Ab and Db major and their modes. F# major keeps sharps.
bool flatKey(int tonicPc, Mode mode) {
    const int major = mod12(tonicPc - relativeMajorOffset(mode));
    return major == 1 || major == 3 || major == 5 || major == 8 || major == 10;
}

bool rootLetter(char c) { return c >= 'A' && c <= 'G'; }

// The root (and slash bass) of a chord symbol moved by `n` semitones; the rest is kept as written.
std::string transposeSymbol(const std::string& sym, int n, bool flats) {
    if (sym.empty() || !rootLetter(sym[0])) return sym;
    std::size_t i = 1;
    if (i < sym.size() && (sym[i] == '#' || sym[i] == 'b')) ++i;
    const int root = pitchClassFromName(sym.substr(0, i));
    if (root < 0) return sym;
    std::string rest = sym.substr(i);
    const auto slash = rest.rfind('/');
    if (slash != std::string::npos && slash + 1 < rest.size() && rootLetter(rest[slash + 1])) {
        const std::string bass = rest.substr(slash + 1);
        const int pc = pitchClassFromName(bass);
        if (pc >= 0) rest = rest.substr(0, slash + 1) + pcName(pc + n, flats);
    }
    return pcName(root + n, flats) + rest;
}

// The compact harmony string with each chord token's symbol moved; spacing, bar marks and rests kept.
std::string transposeHarmony(const std::string& text, int n, bool flats) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (std::isspace(static_cast<unsigned char>(text[i]))) {
            out += text[i++];
            continue;
        }
        std::size_t j = i;
        while (j < text.size() && !std::isspace(static_cast<unsigned char>(text[j]))) ++j;
        const std::string token = text.substr(i, j - i);
        const auto colon = token.find(':');
        if (colon == std::string::npos) out += token;
        else out += transposeSymbol(token.substr(0, colon), n, flats) + token.substr(colon);
        i = j;
    }
    return out;
}

bool usesFlats(const std::string& noteName) { return noteName.size() > 1 && noteName[1] == 'b'; }

std::string partName(const json& p) {
    const auto name = p.value("name", std::string());
    return name.empty() ? p.value("id", std::string("part")) : name;
}

std::string joinNames(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i > 0) out += i + 1 == names.size() ? " and " : ", ";
        out += names[i];
    }
    return out;
}

// Each part's notes for a fixed seed with no humanize, to tell whether a change is audible.
std::map<std::string, std::string> fingerprints(const json& score) {
    std::vector<std::string> warnings;
    RealizeOptions options;
    options.humanize = false;
    const Realization r = realize(parseScore(score.dump(), warnings), options);
    std::map<std::string, std::string> out;
    for (const auto& p : r.parts) {
        std::string f;
        for (const auto& n : p.notes)
            f += std::to_string(n.tick) + ',' + std::to_string(n.dur) + ',' + std::to_string(n.pitch) + ',' +
                 std::to_string(n.vel) + ';';
        out[p.id] = std::move(f);
    }
    return out;
}

bool wholeNumber(std::optional<double> v, int lo, int hi, int& out) {
    if (!v || !std::isfinite(*v) || std::round(*v) != *v || *v < lo || *v > hi || *v == 0.0) return false;
    out = static_cast<int>(*v);
    return true;
}

// Moves a part's literal notes by `n` semitones. False if one would leave MIDI's range.
bool shiftLiterals(json& part, int n) {
    for (auto& b : part["blocks"]) {
        if (!b.is_object() || !b.contains("notes") || !b["notes"].is_array()) continue;
        for (auto& note : b["notes"]) {
            const std::string name = note.value("pitch", std::string());
            const int midi = noteNameToMidi(name);
            if (midi < 0) continue;
            if (midi + n < 0 || midi + n > 127) return false;
        }
    }
    for (auto& b : part["blocks"]) {
        if (!b.is_object() || !b.contains("notes") || !b["notes"].is_array()) continue;
        for (auto& note : b["notes"]) {
            const std::string name = note.value("pitch", std::string());
            const int midi = noteNameToMidi(name);
            if (midi >= 0) note["pitch"] = midiToNoteName(midi + n, usesFlats(name));
        }
    }
    return true;
}

// The literal notes' lowest and highest pitch, or {128, -1} when there are none.
std::pair<int, int> literalSpan(const json& part) {
    int lo = 128, hi = -1;
    for (const auto& b : part["blocks"]) {
        if (!b.is_object() || !b.contains("notes") || !b["notes"].is_array()) continue;
        for (const auto& note : b["notes"]) {
            const int midi = noteNameToMidi(note.value("pitch", std::string()));
            if (midi < 0) continue;
            lo = std::min(lo, midi);
            hi = std::max(hi, midi);
        }
    }
    return {lo, hi};
}

VoicingFamily nextFamily(VoicingFamily f) {
    using V = VoicingFamily;
    switch (f) {
        case V::Close: return V::Drop2;
        case V::Drop2: return V::Open;
        case V::Open: return V::Spread;
        case V::Spread: return V::Close;
        case V::Rootless: return V::Shell;
        case V::Shell: return V::Drop3;
        case V::Drop3: return V::Rootless;
        case V::Quartal: return V::Close;
        case V::Power: return V::Power;
    }
    return V::Close;
}

}  // namespace

std::optional<TweakOp> tweakOpFromString(const std::string& s) {
    if (s == "register") return TweakOp::Register;
    if (s == "transpose") return TweakOp::Transpose;
    if (s == "humanize") return TweakOp::Humanize;
    if (s == "simplify") return TweakOp::Simplify;
    if (s == "intensify") return TweakOp::Intensify;
    if (s == "revoice") return TweakOp::Revoice;
    return std::nullopt;
}

TweakResult tweakJson(const std::string& scoreJson, TweakOp op, const std::vector<std::string>& partIds,
                      std::optional<double> amount) {
    // Validates (throws IrError) and gives the typed view used for roles and the key.
    std::vector<std::string> warnings;
    const Score typed = parseScore(scoreJson, warnings);
    json score = json::parse(scoreJson);

    TweakResult result;
    result.score = scoreJson;
    auto fail = [&](std::string why) {
        result.score = scoreJson;
        result.changed.clear();
        result.error = std::move(why);
        return result;
    };
    const std::set<std::string> wanted(partIds.begin(), partIds.end());
    auto& parts = score["parts"];
    auto roleOf = [&](std::size_t i) { return typed.parts[i].role; };
    std::vector<std::size_t> targets;
    for (std::size_t i = 0; i < parts.size() && i < typed.parts.size(); ++i)
        if (wanted.count(typed.parts[i].id)) targets.push_back(i);
    if (targets.empty()) return fail("None of those parts are in this idea.");

    std::vector<std::size_t> touched;  // parts whose IR changed; kept only if they sound different
    switch (op) {
        case TweakOp::Register: {
            int n = 0;
            if (!wholeNumber(amount, -4, 4, n)) return fail("Register needs a whole number of octaves, -4 to 4.");
            std::vector<std::string> stuck;
            bool anyPitched = false;
            for (auto i : targets) {
                if (roleOf(i) == Role::Drums) continue;
                anyPitched = true;
                auto& p = parts[i];
                const int low = noteNameToMidi(typed.parts[i].low) + 12 * n;
                const int high = noteNameToMidi(typed.parts[i].high) + 12 * n;
                json moved = p;
                if (low < 0 || high > 127 || !shiftLiterals(moved, 12 * n)) {
                    stuck.push_back(partName(p));
                    continue;
                }
                moved["low"] = midiToNoteName(low, usesFlats(typed.parts[i].low));
                moved["high"] = midiToNoteName(high, usesFlats(typed.parts[i].high));
                p = std::move(moved);
                touched.push_back(i);
            }
            if (!anyPitched) return fail("Drums have no register to move.");
            if (touched.empty())
                return fail(joinNames(stuck) + (stuck.size() == 1 ? " is" : " are") + " already as " +
                            (n > 0 ? "high" : "low") + " as it goes.");
            break;
        }
        case TweakOp::Transpose: {
            int n = 0;
            if (!wholeNumber(amount, -11, 11, n)) return fail("Transpose needs a whole number of semitones, -11 to 11.");
            std::vector<std::string> left;
            for (std::size_t i = 0; i < parts.size() && i < typed.parts.size(); ++i)
                if (roleOf(i) != Role::Drums && !wanted.count(typed.parts[i].id)) left.push_back(partName(parts[i]));
            if (!left.empty())
                return fail("Transpose moves the whole idea's key, so it can't leave out " + joinNames(left) + ".");
            const int tonic = mod12(pitchClassFromName(typed.context.tonic) + n);
            const bool flats = flatKey(tonic, typed.context.mode);
            score["context"]["tonic"] = pcName(tonic, flats);
            auto& h = score["harmony"];
            if (h.is_string()) {
                h = transposeHarmony(h.get<std::string>(), n, flats);
            } else if (h.is_array()) {
                for (auto& c : h)
                    if (c.is_object() && c.contains("symbol") && c["symbol"].is_string())
                        c["symbol"] = transposeSymbol(c["symbol"].get<std::string>(), n, flats);
            }
            for (std::size_t i = 0; i < parts.size() && i < typed.parts.size(); ++i) {
                if (roleOf(i) == Role::Drums) continue;
                auto& p = parts[i];
                if (!shiftLiterals(p, n)) return fail(partName(p) + " has notes that would leave the MIDI range.");
                // Played notes keep their register: widen the range rather than fold them.
                const auto [lo, hi] = literalSpan(p);
                if (lo <= hi) {
                    if (lo < noteNameToMidi(typed.parts[i].low)) p["low"] = midiToNoteName(lo);
                    if (hi > noteNameToMidi(typed.parts[i].high)) p["high"] = midiToNoteName(hi);
                }
                result.changed.push_back(typed.parts[i].id);
            }
            if (result.changed.empty()) return fail("Drums have no key to move.");
            result.score = score.dump();
            return result;
        }
        case TweakOp::Humanize: {
            if (!amount || !std::isfinite(*amount) || *amount < 0.0 || *amount > 1.0)
                return fail("Humanize needs an amount from 0 to 1.");
            for (auto i : targets) {
                const double was = typed.parts[i].humanize.value_or(kDefaultHumanize);
                if (std::abs(was - *amount) < 1e-9) continue;
                parts[i]["humanize"] = *amount;
                result.changed.push_back(typed.parts[i].id);
            }
            if (result.changed.empty()) return fail("Humanize is already at that amount.");
            result.score = score.dump();
            return result;
        }
        case TweakOp::Simplify:
        case TweakOp::Intensify: {
            const double step = op == TweakOp::Simplify ? -kDensityStep : kDensityStep;
            for (auto i : targets) {
                const double was = typed.parts[i].density.value_or(0.5);
                const double now = std::clamp(was + step, 0.0, 1.0);
                if (now == was) continue;
                parts[i]["density"] = now;
                touched.push_back(i);
            }
            break;
        }
        case TweakOp::Revoice: break;  // below: it may need several tries per part
    }

    const auto before = fingerprints(json::parse(scoreJson));
    if (op == TweakOp::Revoice) {
        bool anyChords = false;
        for (auto i : targets) {
            const Role role = roleOf(i);
            if (role != Role::Chords && role != Role::Pad) continue;
            anyChords = true;
            const VoicingFamily fallback = role == Role::Pad ? VoicingFamily::Open : VoicingFamily::Close;
            const auto& blocks = typed.parts[i].blocks;
            // Each block moves to the next family; if that sounds the same (a fallback voicing), the next.
            json original = parts[i];
            for (int tries = 0; tries < 4; ++tries) {
                bool moved = false;
                for (std::size_t b = 0; b < blocks.size(); ++b) {
                    const auto& current = parts[i]["blocks"][b];
                    const auto was = current.contains("voicing") && current["voicing"].is_string()
                                         ? voicingFromString(current["voicing"].get<std::string>())
                                         : std::nullopt;
                    const VoicingFamily next = nextFamily(was.value_or(blocks[b].voicing.value_or(fallback)));
                    if (next == was.value_or(fallback)) continue;
                    parts[i]["blocks"][b]["voicing"] = toString(next);
                    moved = true;
                }
                if (!moved) break;
                if (fingerprints(score)[typed.parts[i].id] != before.at(typed.parts[i].id)) break;
            }
            if (fingerprints(score)[typed.parts[i].id] == before.at(typed.parts[i].id)) parts[i] = original;
            else touched.push_back(i);
        }
        if (!anyChords) return fail("Only chords and pads can be revoiced.");
    }

    // Keep only the parts that now sound different.
    const auto after = touched.empty() ? before : fingerprints(score);
    json original = json::parse(scoreJson);
    for (auto i : touched) {
        const auto& id = typed.parts[i].id;
        if (after.at(id) != before.at(id)) result.changed.push_back(id);
        else parts[i] = original["parts"][i];
    }
    if (result.changed.empty()) {
        std::vector<std::string> names;
        for (auto i : targets) names.push_back(partName(parts[i]));
        switch (op) {
            case TweakOp::Simplify: return fail(joinNames(names) + " can't get any simpler.");
            case TweakOp::Intensify: return fail(joinNames(names) + " can't get any busier.");
            case TweakOp::Revoice: return fail(joinNames(names) + " has no other voicing that sounds different.");
            default: return fail("Nothing changed.");
        }
    }
    // Score order, whatever order the parts were touched in.
    std::vector<std::string> ordered;
    for (const auto& p : typed.parts)
        if (std::find(result.changed.begin(), result.changed.end(), p.id) != result.changed.end()) ordered.push_back(p.id);
    result.changed = std::move(ordered);
    result.score = score.dump();
    return result;
}

}  // namespace flowstate
