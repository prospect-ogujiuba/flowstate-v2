#include "flowstate/ir.h"

#include "flowstate/theory.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

namespace flowstate {

using nlohmann::json;

namespace {

template <typename E, std::size_t N>
std::optional<E> lookup(const std::array<std::pair<const char*, E>, N>& table, const std::string& s) {
    for (const auto& [name, value] : table)
        if (s == name) return value;
    return std::nullopt;
}

template <typename E, std::size_t N>
const char* reverseLookup(const std::array<std::pair<const char*, E>, N>& table, E value) {
    for (const auto& [name, v] : table)
        if (v == value) return name;
    return "?";
}

const std::array<std::pair<const char*, Role>, 7> kRoles{{
    {"chords", Role::Chords}, {"pad", Role::Pad}, {"arp", Role::Arp}, {"bass", Role::Bass},
    {"melody", Role::Melody}, {"counter", Role::Counter}, {"drums", Role::Drums}}};

const std::array<std::pair<const char*, Mode>, 12> kModes{{
    {"major", Mode::Major}, {"minor", Mode::Minor}, {"dorian", Mode::Dorian},
    {"phrygian", Mode::Phrygian}, {"lydian", Mode::Lydian}, {"mixolydian", Mode::Mixolydian},
    {"locrian", Mode::Locrian}, {"harmonic_minor", Mode::HarmonicMinor},
    {"melodic_minor", Mode::MelodicMinor}, {"major_pentatonic", Mode::MajorPentatonic},
    {"minor_pentatonic", Mode::MinorPentatonic}, {"blues", Mode::Blues}}};

const std::array<std::pair<const char*, VoicingFamily>, 9> kVoicings{{
    {"close", VoicingFamily::Close}, {"open", VoicingFamily::Open}, {"drop2", VoicingFamily::Drop2},
    {"drop3", VoicingFamily::Drop3}, {"rootless", VoicingFamily::Rootless},
    {"shell", VoicingFamily::Shell}, {"spread", VoicingFamily::Spread},
    {"quartal", VoicingFamily::Quartal}, {"power", VoicingFamily::Power}}};

const std::array<std::pair<const char*, ArpPattern>, 5> kArps{{
    {"up", ArpPattern::Up}, {"down", ArpPattern::Down}, {"updown", ArpPattern::UpDown},
    {"random", ArpPattern::Random}, {"chord_tones", ArpPattern::ChordTones}}};

const std::array<std::pair<const char*, Articulation>, 3> kArticulations{{
    {"legato", Articulation::Legato}, {"normal", Articulation::Normal},
    {"staccato", Articulation::Staccato}}};

const std::array<std::pair<const char*, DrumVoice>, 16> kDrumVoices{{
    {"kick", DrumVoice::Kick}, {"snare", DrumVoice::Snare}, {"clap", DrumVoice::Clap},
    {"rim", DrumVoice::Rim}, {"closed_hat", DrumVoice::ClosedHat},
    {"pedal_hat", DrumVoice::PedalHat}, {"open_hat", DrumVoice::OpenHat},
    {"low_tom", DrumVoice::LowTom}, {"mid_tom", DrumVoice::MidTom},
    {"high_tom", DrumVoice::HighTom}, {"crash", DrumVoice::Crash}, {"ride", DrumVoice::Ride},
    {"ride_bell", DrumVoice::RideBell}, {"shaker", DrumVoice::Shaker},
    {"tambourine", DrumVoice::Tambourine}, {"cowbell", DrumVoice::Cowbell}}};

const std::array<std::pair<const char*, Fill>, 6> kFills{{
    {"none", Fill::None}, {"snare_roll", Fill::SnareRoll}, {"tom_run", Fill::TomRun},
    {"kick_build", Fill::KickBuild}, {"crash_end", Fill::CrashEnd},
    {"half_time_break", Fill::HalfTimeBreak}}};

// Small reader that records warnings with a JSON-path-like prefix.
class Reader {
public:
    explicit Reader(std::vector<std::string>& warnings) : warnings_(warnings) {}

    void warn(const std::string& path, const std::string& msg) { warnings_.push_back(path + ": " + msg); }

    bool has(const json& obj, const char* key) const {
        return obj.is_object() && obj.contains(key) && !obj.at(key).is_null();
    }

    double number(const json& obj, const char* key, double fallback, const std::string& path,
                  bool warnIfMissing = true) {
        if (!obj.is_object() || !obj.contains(key)) {
            if (warnIfMissing) warn(path + "." + key, "missing; using " + fmt(fallback));
            return fallback;
        }
        const json& v = obj.at(key);
        if (!v.is_number()) {
            if (warnIfMissing || !v.is_null())
                warn(path + "." + key, "not a number; using " + fmt(fallback));
            return fallback;
        }
        double d = v.get<double>();
        if (!std::isfinite(d)) {
            warn(path + "." + key, "not finite; using " + fmt(fallback));
            return fallback;
        }
        return d;
    }

    int integer(const json& obj, const char* key, int fallback, const std::string& path,
                bool warnIfMissing = true) {
        double d = number(obj, key, fallback, path, warnIfMissing);
        double r = std::round(d);
        if (r != d) warn(path + "." + key, "expected integer; rounded");
        if (r > 1e9) r = 1e9;
        if (r < -1e9) r = -1e9;
        return static_cast<int>(r);
    }

    std::string string(const json& obj, const char* key, const std::string& fallback,
                       const std::string& path, bool warnIfMissing = true) {
        if (!obj.is_object() || !obj.contains(key) || !obj.at(key).is_string()) {
            if (warnIfMissing) warn(path + "." + key, "missing or not a string");
            return fallback;
        }
        return obj.at(key).get<std::string>();
    }

    std::optional<std::string> nullableString(const json& obj, const char* key, const std::string& path) {
        if (!has(obj, key)) return std::nullopt;
        if (!obj.at(key).is_string()) {
            warn(path + "." + key, "not a string; ignored");
            return std::nullopt;
        }
        return obj.at(key).get<std::string>();
    }

    bool boolean(const json& obj, const char* key, bool fallback, const std::string& path) {
        if (!has(obj, key)) return fallback;
        if (!obj.at(key).is_boolean()) {
            warn(path + "." + key, "not a boolean");
            return fallback;
        }
        return obj.at(key).get<bool>();
    }

    const json* array(const json& obj, const char* key, const std::string& path, bool warnIfMissing) {
        if (!has(obj, key)) {
            if (warnIfMissing) warn(path + "." + key, "missing; treated as empty");
            return nullptr;
        }
        if (!obj.at(key).is_array()) {
            warn(path + "." + key, "not an array; ignored");
            return nullptr;
        }
        return &obj.at(key);
    }

    template <typename E>
    std::optional<E> enumeration(const json& obj, const char* key, const std::string& path,
                                 std::optional<E> (*parse)(const std::string&)) {
        auto s = nullableString(obj, key, path);
        if (!s) return std::nullopt;
        auto v = parse(*s);
        if (!v) warn(path + "." + key, "unknown value \"" + *s + "\"; ignored");
        return v;
    }

private:
    static std::string fmt(double d) {
        json j = d;
        return j.dump();
    }
    std::vector<std::string>& warnings_;
};

template <typename T>
T clampWarn(Reader& r, T value, T lo, T hi, const std::string& path) {
    if (value < lo || value > hi) {
        T c = std::clamp(value, lo, hi);
        json jv = value, jc = c;
        r.warn(path, "value " + jv.dump() + " out of range; clamped to " + jc.dump());
        return c;
    }
    return value;
}

// A motif's notes in the compact string form: whitespace-separated tokens played one after another.
// "<pitch>:<beats>[!]", where pitch is "r" (rest) or [b|#]<degree> plus "+"/"-" marks per octave up/down,
// and beats is a decimal or a fraction ("1/3"). "|" tokens are visual bar marks and are ignored.
// Onsets are the running sum of durations. Bad tokens are skipped with a warning.
std::optional<double> parseBeats(const std::string& t) {
    if (t.empty()) return std::nullopt;
    auto parseDecimal = [](const std::string& d) -> std::optional<double> {
        if (d.empty() || d.find_first_not_of("0123456789.") != std::string::npos) return std::nullopt;
        if (std::count(d.begin(), d.end(), '.') > 1 || d == ".") return std::nullopt;
        return std::stod(d.front() == '.' ? "0" + d : d);
    };
    std::optional<double> v;
    if (auto slash = t.find('/'); slash != std::string::npos) {
        auto num = parseDecimal(t.substr(0, slash)), den = parseDecimal(t.substr(slash + 1));
        if (num && den && *den > 0) v = *num / *den;
    } else {
        v = parseDecimal(t);
    }
    if (!v || !std::isfinite(*v) || *v <= 0) return std::nullopt;
    return v;
}

std::vector<MotifNote> parseMotifString(Reader& r, const std::string& text, const std::string& path) {
    std::vector<MotifNote> notes;
    double at = 0.0;
    std::size_t index = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t start = text.find_first_not_of(" \t\n\r", pos);
        if (start == std::string::npos) break;
        std::size_t end = text.find_first_of(" \t\n\r", start);
        if (end == std::string::npos) end = text.size();
        std::string tok = text.substr(start, end - start);
        pos = end;
        if (tok == "|") continue;
        std::string tp = path + " token " + std::to_string(++index) + " \"" + tok + "\"";

        bool accent = !tok.empty() && tok.back() == '!';
        if (accent) tok.pop_back();
        auto colon = tok.find(':');
        if (colon == std::string::npos) {
            r.warn(tp, "expected <pitch>:<beats>; skipped");
            continue;
        }
        auto beats = parseBeats(tok.substr(colon + 1));
        if (!beats) {
            r.warn(tp, "duration must be a positive number or fraction; skipped");
            continue;
        }
        std::string pitch = tok.substr(0, colon);
        if (pitch == "r") {
            at += *beats;
            continue;
        }
        MotifNote mn;
        mn.beat = at;
        mn.beats = *beats;
        mn.accent = accent;
        std::size_t i = 0;
        if (i < pitch.size() && (pitch[i] == 'b' || pitch[i] == '#')) mn.alter = pitch[i++] == 'b' ? -1 : 1;
        std::size_t digits = i;
        while (digits < pitch.size() && std::isdigit(static_cast<unsigned char>(pitch[digits]))) ++digits;
        std::string marks = pitch.substr(digits);
        bool up = marks.find_first_not_of('+') == std::string::npos;
        bool down = marks.find_first_not_of('-') == std::string::npos;
        if (digits == i || digits - i > 2 || (!up && !down)) {
            r.warn(tp, "unknown pitch; played as a rest");
            at += *beats;
            continue;
        }
        mn.degree = std::stoi(pitch.substr(i, digits - i));
        int octave = static_cast<int>(marks.size()) * (up ? 1 : -1);
        mn.octave = clampWarn(r, octave, -4, 4, tp + " octave");
        notes.push_back(mn);
        at += *beats;
    }
    return notes;
}

Context parseContext(Reader& r, const json& j) {
    const std::string p = "context";
    if (!j.is_object()) throw IrError("context: missing or not an object");
    Context c;
    c.tempo = clampWarn(r, r.number(j, "tempo", 120.0, p), 20.0, 400.0, p + ".tempo");
    c.meterNumerator = clampWarn(r, r.integer(j, "meterNumerator", 4, p), 1, 32, p + ".meterNumerator");
    int den = r.integer(j, "meterDenominator", 4, p);
    if (den != 1 && den != 2 && den != 4 && den != 8 && den != 16 && den != 32) {
        r.warn(p + ".meterDenominator", "must be a power of two in 1..32; using 4");
        den = 4;
    }
    c.meterDenominator = den;
    if (!j.contains("tonic") || !j.at("tonic").is_string())
        throw IrError("context.tonic: missing or not a string");
    c.tonic = j.at("tonic").get<std::string>();
    if (pitchClassFromName(c.tonic) < 0 || c.tonic.size() > 2)
        throw IrError("context.tonic: unknown tonic \"" + c.tonic + "\"");
    if (!j.contains("mode") || !j.at("mode").is_string())
        throw IrError("context.mode: missing or not a string");
    auto mode = modeFromString(j.at("mode").get<std::string>());
    if (!mode) throw IrError("context.mode: unknown mode \"" + j.at("mode").get<std::string>() + "\"");
    c.mode = *mode;
    c.bars = clampWarn(r, r.integer(j, "bars", 4, p), 1, 512, p + ".bars");
    c.swing = clampWarn(r, r.number(j, "swing", 0.0, p), 0.0, 0.75, p + ".swing");
    if (const json* style = r.array(j, "style", p, false))
        for (const auto& s : *style)
            if (s.is_string()) c.style.push_back(s.get<std::string>());
    return c;
}

std::vector<LiteralNote> parseLiteralNotes(Reader& r, const json& arr, const std::string& p) {
    std::vector<LiteralNote> out;
    for (std::size_t i = 0; i < arr.size(); ++i) {
        const json& n = arr[i];
        std::string np = p + "[" + std::to_string(i) + "]";
        if (!n.is_object()) {
            r.warn(np, "not an object; skipped");
            continue;
        }
        LiteralNote ln;
        ln.bar = r.integer(n, "bar", 1, np);
        ln.beat = r.number(n, "beat", 1.0, np);
        ln.beats = r.number(n, "beats", 1.0, np);
        ln.pitch = r.string(n, "pitch", "", np);
        ln.velocity = clampWarn(r, r.integer(n, "velocity", 100, np), 1, 127, np + ".velocity");
        if (ln.pitch.empty()) {
            r.warn(np, "missing pitch; skipped");
            continue;
        }
        out.push_back(ln);
    }
    return out;
}

Block parseBlock(Reader& r, const json& b, const std::string& p) {
    Block blk;
    blk.startBar = r.integer(b, "startBar", 1, p);
    blk.endBar = r.integer(b, "endBar", blk.startBar, p);
    blk.rhythm = r.nullableString(b, "rhythm", p);
    blk.voicing = r.enumeration<VoicingFamily>(b, "voicing", p, voicingFromString);
    blk.arpPattern = r.enumeration<ArpPattern>(b, "arpPattern", p, arpPatternFromString);
    blk.motif = r.nullableString(b, "motif", p);
    if (const json* t = r.array(b, "transforms", p, false)) {
        std::vector<std::string> ts;
        for (const auto& s : *t)
            if (s.is_string()) ts.push_back(s.get<std::string>());
        blk.transforms = ts;
    }
    if (r.has(b, "repeatEvery")) blk.repeatEvery = r.number(b, "repeatEvery", 0.0, p);
    if (const json* d = r.array(b, "drums", p, false)) {
        std::vector<DrumLane> lanes;
        for (std::size_t i = 0; i < d->size(); ++i) {
            const json& lane = (*d)[i];
            std::string lp = p + ".drums[" + std::to_string(i) + "]";
            auto voice = r.enumeration<DrumVoice>(lane, "voice", lp, drumVoiceFromString);
            if (!voice) {
                r.warn(lp, "lane skipped (no valid voice)");
                continue;
            }
            lanes.push_back({*voice, r.string(lane, "steps", "", lp)});
        }
        blk.drums = lanes;
    }
    blk.fill = r.enumeration<Fill>(b, "fill", p, fillFromString);
    if (const json* n = r.array(b, "notes", p, false)) blk.notes = parseLiteralNotes(r, *n, p + ".notes");
    blk.articulation = r.enumeration<Articulation>(b, "articulation", p, articulationFromString);
    return blk;
}

Part parsePart(Reader& r, const json& j, const std::string& p) {
    if (!j.is_object()) throw IrError(p + ": not an object");
    Part part;
    part.id = r.string(j, "id", "", p);
    if (!j.contains("role") || !j.at("role").is_string()) throw IrError(p + ".role: missing");
    auto role = roleFromString(j.at("role").get<std::string>());
    if (!role) throw IrError(p + ".role: unknown role \"" + j.at("role").get<std::string>() + "\"");
    part.role = *role;
    part.name = r.string(j, "name", part.id, p);
    const bool drums = part.role == Role::Drums;
    const char* defLow = part.role == Role::Bass ? "E1" : drums ? "C1" : "C3";
    const char* defHigh = part.role == Role::Bass ? "G3" : drums ? "C6" : "C6";
    part.low = r.string(j, "low", defLow, p);
    part.high = r.string(j, "high", defHigh, p);
    if (noteNameToMidi(part.low) < 0) {
        r.warn(p + ".low", "invalid note name \"" + part.low + "\"; using " + defLow);
        part.low = defLow;
    }
    if (noteNameToMidi(part.high) < 0) {
        r.warn(p + ".high", "invalid note name \"" + part.high + "\"; using " + defHigh);
        part.high = defHigh;
    }
    if (noteNameToMidi(part.low) > noteNameToMidi(part.high)) {
        r.warn(p, "low is above high; swapped");
        std::swap(part.low, part.high);
    }
    part.grid = clampWarn(r, r.integer(j, "grid", 4, p), 1, 16, p + ".grid");
    part.velocity = clampWarn(r, r.integer(j, "velocity", 90, p), 1, 127, p + ".velocity");
    if (const json* blocks = r.array(j, "blocks", p, true)) {
        for (std::size_t i = 0; i < blocks->size(); ++i) {
            const json& b = (*blocks)[i];
            std::string bp = p + ".blocks[" + std::to_string(i) + "]";
            if (!b.is_object()) {
                r.warn(bp, "not an object; skipped");
                continue;
            }
            part.blocks.push_back(parseBlock(r, b, bp));
        }
    }
    return part;
}

}  // namespace

Score parseScore(const std::string& jsonText, std::vector<std::string>& warnings) {
    json j;
    try {
        j = json::parse(jsonText);
    } catch (const json::parse_error& e) {
        throw IrError(std::string("invalid JSON: ") + e.what());
    }
    if (!j.is_object()) throw IrError("top level is not a JSON object");
    if (!j.contains("ir") || !j.at("ir").is_string())
        throw IrError("missing \"ir\" id (expected \"" + std::string(kIrId) + "\")");
    if (j.at("ir").get<std::string>() != kIrId)
        throw IrError("unsupported ir id \"" + j.at("ir").get<std::string>() + "\" (expected \"" +
                      std::string(kIrId) + "\")");

    Reader r(warnings);
    Score s;
    s.title = r.string(j, "title", "", "score");
    if (!j.contains("context")) throw IrError("context: missing");
    s.context = parseContext(r, j.at("context"));

    if (const json* form = r.array(j, "form", "score", true)) {
        for (std::size_t i = 0; i < form->size(); ++i) {
            const json& f = (*form)[i];
            std::string fp = "form[" + std::to_string(i) + "]";
            Section sec;
            sec.name = r.string(f, "name", "", fp, false);
            sec.startBar = r.integer(f, "startBar", 1, fp);
            sec.bars = r.integer(f, "bars", 1, fp);
            sec.energy = clampWarn(r, r.number(f, "energy", 0.6, fp), 0.0, 1.0, fp + ".energy");
            s.form.push_back(sec);
        }
    }
    if (const json* harmony = r.array(j, "harmony", "score", true)) {
        for (std::size_t i = 0; i < harmony->size(); ++i) {
            const json& c = (*harmony)[i];
            std::string cp = "harmony[" + std::to_string(i) + "]";
            ChordEntry ce;
            ce.bar = r.integer(c, "bar", 1, cp);
            ce.beat = r.number(c, "beat", 1.0, cp);
            ce.beats = r.number(c, "beats", 0.0, cp);
            ce.symbol = r.string(c, "symbol", "", cp);
            s.harmony.push_back(ce);
        }
    }
    if (const json* motifs = r.array(j, "motifs", "score", true)) {
        for (std::size_t i = 0; i < motifs->size(); ++i) {
            const json& m = (*motifs)[i];
            std::string mp = "motifs[" + std::to_string(i) + "]";
            Motif motif;
            motif.id = r.string(m, "id", "", mp);
            if (m.is_object() && m.contains("notes") && m.at("notes").is_string()) {
                motif.notes = parseMotifString(r, m.at("notes").get<std::string>(), mp + ".notes");
            } else if (const json* notes = r.array(m, "notes", mp, true)) {
                for (std::size_t k = 0; k < notes->size(); ++k) {
                    const json& n = (*notes)[k];
                    std::string np = mp + ".notes[" + std::to_string(k) + "]";
                    MotifNote mn;
                    mn.degree = r.integer(n, "degree", 1, np);
                    mn.octave = clampWarn(r, r.integer(n, "octave", 0, np), -4, 4, np + ".octave");
                    mn.alter = clampWarn(r, r.integer(n, "alter", 0, np, false), -1, 1, np + ".alter");
                    mn.beat = r.number(n, "beat", 0.0, np);
                    mn.beats = r.number(n, "beats", 1.0, np);
                    mn.accent = r.boolean(n, "accent", false, np);
                    motif.notes.push_back(mn);
                }
            }
            s.motifs.push_back(motif);
        }
    }
    if (!j.contains("parts") || !j.at("parts").is_array()) throw IrError("parts: missing or not an array");
    const json& parts = j.at("parts");
    for (std::size_t i = 0; i < parts.size(); ++i)
        s.parts.push_back(parsePart(r, parts[i], "parts[" + std::to_string(i) + "]"));

    validateScore(s, warnings);
    return s;
}

void validateScore(const Score& score, std::vector<std::string>& warnings) {
    const int bars = score.context.bars;
    // Form tiling.
    if (!score.form.empty()) {
        std::vector<int> cover(static_cast<std::size_t>(bars) + 1, 0);
        for (const auto& sec : score.form) {
            if (sec.bars < 1) warnings.push_back("form: section \"" + sec.name + "\" has no bars");
            for (int b = sec.startBar; b < sec.startBar + sec.bars; ++b) {
                if (b < 1 || b > bars) {
                    warnings.push_back("form: section \"" + sec.name + "\" extends outside the clip");
                    break;
                }
                cover[static_cast<std::size_t>(b)]++;
            }
        }
        for (int b = 1; b <= bars; ++b) {
            if (cover[static_cast<std::size_t>(b)] == 0) {
                warnings.push_back("form: bar " + std::to_string(b) + " not covered; energy 0.6 used");
            } else if (cover[static_cast<std::size_t>(b)] > 1) {
                warnings.push_back("form: bar " + std::to_string(b) + " covered by several sections; first wins");
            }
        }
    }
    std::set<std::string> ids;
    std::set<std::string> motifIds;
    for (const auto& m : score.motifs) motifIds.insert(m.id);
    for (const auto& part : score.parts) {
        if (!ids.insert(part.id).second) warnings.push_back("parts: duplicate part id \"" + part.id + "\"");
        std::vector<std::pair<int, int>> ranges;
        for (const auto& b : part.blocks) {
            std::string bp = "part " + part.id + " block " + std::to_string(b.startBar) + ".." +
                             std::to_string(b.endBar);
            if (b.endBar < b.startBar) warnings.push_back(bp + ": endBar before startBar; block ignored");
            if (b.startBar > bars) warnings.push_back(bp + ": starts after the clip end; ignored");
            else if (b.endBar > bars) warnings.push_back(bp + ": extends past the clip; clipped");
            if (b.motif && !motifIds.count(*b.motif))
                warnings.push_back(bp + ": unknown motif \"" + *b.motif + "\"");
            for (const auto& [s, e] : ranges)
                if (b.startBar <= e && s <= b.endBar) {
                    warnings.push_back(bp + ": overlaps another block; later block wins in the overlap");
                    break;
                }
            ranges.emplace_back(b.startBar, b.endBar);
        }
    }
}

const char* toString(Role role) { return reverseLookup(kRoles, role); }
const char* toString(DrumVoice voice) { return reverseLookup(kDrumVoices, voice); }
const char* toString(VoicingFamily family) { return reverseLookup(kVoicings, family); }
std::optional<Role> roleFromString(const std::string& s) { return lookup(kRoles, s); }
std::optional<Mode> modeFromString(const std::string& s) { return lookup(kModes, s); }
std::optional<VoicingFamily> voicingFromString(const std::string& s) { return lookup(kVoicings, s); }
std::optional<ArpPattern> arpPatternFromString(const std::string& s) { return lookup(kArps, s); }
std::optional<Articulation> articulationFromString(const std::string& s) { return lookup(kArticulations, s); }
std::optional<DrumVoice> drumVoiceFromString(const std::string& s) { return lookup(kDrumVoices, s); }
std::optional<Fill> fillFromString(const std::string& s) { return lookup(kFills, s); }

bool isPitchedMonophonic(Role role) {
    return role == Role::Bass || role == Role::Melody || role == Role::Counter;
}

}  // namespace flowstate
