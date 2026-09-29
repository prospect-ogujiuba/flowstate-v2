#include "flowstate/theory.h"

#include <algorithm>
#include <cctype>

namespace flowstate {

int pitchClassFromName(const std::string& name) {
    if (name.empty()) return -1;
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // A..G
    char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    if (c < 'A' || c > 'G') return -1;
    int pc = base[c - 'A'];
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (name[i] == '#') ++pc;
        else if (name[i] == 'b') --pc;
        else return -1;
    }
    return mod12(pc);
}

int noteNameToMidi(const std::string& name) {
    if (name.size() < 2) return -1;
    std::size_t i = 1;
    while (i < name.size() && (name[i] == '#' || name[i] == 'b')) ++i;
    int pc = pitchClassFromName(name.substr(0, i));
    if (pc < 0 || i >= name.size()) return -1;
    std::string oct = name.substr(i);
    bool neg = false;
    std::size_t k = 0;
    if (oct[0] == '-') {
        neg = true;
        k = 1;
    }
    if (k >= oct.size() || oct.size() - k > 2) return -1;
    int o = 0;
    for (; k < oct.size(); ++k) {
        if (!std::isdigit(static_cast<unsigned char>(oct[k]))) return -1;
        o = o * 10 + (oct[k] - '0');
    }
    if (neg) o = -o;
    // Accidentals can cross octave boundaries (B#3 = C4, Cb4 = B3).
    int letterBase = pitchClassFromName(name.substr(0, 1));
    int accidental = 0;
    for (std::size_t a = 1; a < i; ++a) accidental += name[a] == '#' ? 1 : -1;
    int midi = (o + 1) * 12 + letterBase + accidental;
    return (midi < 0 || midi > 127) ? -1 : midi;
}

std::string midiToNoteName(int midi, bool preferFlats) {
    static const char* sharps[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* flats[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    int pc = mod12(midi);
    int octave = (midi - pc) / 12 - 1;
    return std::string(preferFlats ? flats[pc] : sharps[pc]) + std::to_string(octave);
}

bool Scale::contains(int pitch) const {
    int rel = mod12(pitch - tonic);
    return std::find(intervals.begin(), intervals.end(), rel) != intervals.end();
}

int Scale::degreeOffset(int degree) const {
    const int n = size();
    int idx = degree - 1;
    int oct = idx >= 0 ? idx / n : -((-idx + n - 1) / n);
    int pos = idx - oct * n;
    return oct * 12 + intervals[static_cast<std::size_t>(pos)];
}

int Scale::stepUp(int pitch) const {
    for (int p = pitch + 1; p <= pitch + 12; ++p)
        if (contains(p)) return p;
    return pitch + 1;
}

int Scale::stepDown(int pitch) const {
    for (int p = pitch - 1; p >= pitch - 12; --p)
        if (contains(p)) return p;
    return pitch - 1;
}

int Scale::snap(int pitch) const {
    for (int d = 0; d <= 6; ++d) {
        if (contains(pitch - d)) return pitch - d;
        if (contains(pitch + d)) return pitch + d;
    }
    return pitch;
}

Scale makeScale(int tonicPc, Mode mode) {
    Scale s;
    s.tonic = mod12(tonicPc);
    switch (mode) {
        case Mode::Major: s.intervals = {0, 2, 4, 5, 7, 9, 11}; break;
        case Mode::Minor: s.intervals = {0, 2, 3, 5, 7, 8, 10}; break;
        case Mode::Dorian: s.intervals = {0, 2, 3, 5, 7, 9, 10}; break;
        case Mode::Phrygian: s.intervals = {0, 1, 3, 5, 7, 8, 10}; break;
        case Mode::Lydian: s.intervals = {0, 2, 4, 6, 7, 9, 11}; break;
        case Mode::Mixolydian: s.intervals = {0, 2, 4, 5, 7, 9, 10}; break;
        case Mode::Locrian: s.intervals = {0, 1, 3, 5, 6, 8, 10}; break;
        case Mode::HarmonicMinor: s.intervals = {0, 2, 3, 5, 7, 8, 11}; break;
        case Mode::MelodicMinor: s.intervals = {0, 2, 3, 5, 7, 9, 11}; break;
        case Mode::MajorPentatonic: s.intervals = {0, 2, 4, 7, 9}; break;
        case Mode::MinorPentatonic: s.intervals = {0, 3, 5, 7, 10}; break;
        case Mode::Blues: s.intervals = {0, 3, 5, 6, 7, 10}; break;
    }
    return s;
}

Scale makeScale(const Context& ctx) { return makeScale(pitchClassFromName(ctx.tonic), ctx.mode); }

bool Chord::hasTension(int interval) const {
    return std::find(tensions.begin(), tensions.end(), interval) != tensions.end();
}

std::vector<int> Chord::intervals() const {
    std::vector<int> v{0};
    if (third >= 0) v.push_back(third);
    if (fifth >= 0) v.push_back(fifth);
    if (sixth >= 0) v.push_back(sixth);
    if (seventh >= 0) v.push_back(seventh);
    for (int t : tensions) v.push_back(t);
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

std::vector<int> Chord::pitchClasses() const {
    std::vector<int> pcs;
    for (int i : intervals()) pcs.push_back(mod12(root + i));
    if (bass >= 0) pcs.push_back(bass);
    std::sort(pcs.begin(), pcs.end());
    pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
    return pcs;
}

bool Chord::containsPc(int pc) const {
    auto pcs = pitchClasses();
    return std::find(pcs.begin(), pcs.end(), mod12(pc)) != pcs.end();
}

Chord diatonicChord(const Scale& scale, int degree, bool seventh) {
    Chord c;
    int rootOff = scale.degreeOffset(degree);
    c.root = mod12(scale.tonic + rootOff);
    // Works for 5/6-note scales too: take the first preferred interval that
    // is a scale member.
    auto firstMember = [&](const std::vector<int>& prefs) {
        for (int p : prefs)
            if (scale.contains(c.root + p)) return p;
        return prefs.front();
    };
    c.third = firstMember({3, 4, 5, 2});
    c.sus = c.third == 5 || c.third == 2;
    c.fifth = firstMember({7, 6, 8});
    const std::string name = midiToNoteName(c.root + 60);
    c.symbol = name.substr(0, name.size() - 1) + (c.third == 3 ? "m" : c.sus ? "sus" : "");
    if (seventh) c.seventh = firstMember({10, 11, 9});
    return c;
}

}  // namespace flowstate
