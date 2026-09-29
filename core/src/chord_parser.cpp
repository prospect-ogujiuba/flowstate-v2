// Chord symbol parser: root, quality, extension, alterations, slash bass.
#include "flowstate/theory.h"

#include <algorithm>
#include <cctype>

namespace flowstate {

namespace {

bool startsWith(const std::string& s, std::size_t pos, const char* prefix) {
    return s.compare(pos, std::char_traits<char>::length(prefix), prefix) == 0;
}

void addTension(Chord& c, int interval) {
    if (!c.hasTension(interval)) c.tensions.push_back(interval);
}

void removeTension(Chord& c, int interval) {
    c.tensions.erase(std::remove(c.tensions.begin(), c.tensions.end(), interval), c.tensions.end());
}

// Reads an accidental-prefixed number ("b9", "#11", "+5", "-13", "9").
bool readAlteration(const std::string& s, std::size_t& pos, int& accidental, int& number) {
    std::size_t p = pos;
    accidental = 0;
    if (p < s.size() && (s[p] == 'b' || s[p] == '-')) {
        accidental = -1;
        ++p;
    } else if (p < s.size() && (s[p] == '#' || s[p] == '+')) {
        accidental = 1;
        ++p;
    }
    if (p >= s.size() || !std::isdigit(static_cast<unsigned char>(s[p]))) return false;
    number = 0;
    while (p < s.size() && std::isdigit(static_cast<unsigned char>(s[p]))) number = number * 10 + (s[p++] - '0');
    pos = p;
    return true;
}

}  // namespace

ChordParse parseChord(const std::string& symbolIn) {
    ChordParse result;
    std::string symbol;
    for (char ch : symbolIn)
        if (!std::isspace(static_cast<unsigned char>(ch))) symbol += ch;
    if (symbol.empty()) {
        result.warning = "empty chord symbol";
        return result;
    }
    // Normalise common unicode quality marks to ASCII.
    auto replaceAll = [&](const std::string& from, const std::string& to) {
        for (std::size_t p = symbol.find(from); p != std::string::npos; p = symbol.find(from, p + to.size()))
            symbol.replace(p, from.size(), to);
    };
    replaceAll("\xC3\xB8", "h");      // ø half-diminished
    replaceAll("\xC2\xB0", "o");      // ° diminished
    replaceAll("\xCE\x94", "maj");    // Δ
    replaceAll("\xE2\x99\xAF", "#");  // ♯
    replaceAll("\xE2\x99\xAD", "b");  // ♭
    replaceAll("6/9", "69");

    Chord c;
    c.symbol = symbolIn;
    std::size_t pos = 1;
    if (pos < symbol.size() && (symbol[pos] == '#' || symbol[pos] == 'b')) ++pos;
    c.root = pitchClassFromName(symbol.substr(0, pos));
    if (c.root < 0) {
        result.warning = "unrecognised chord root in \"" + symbolIn + "\"";
        return result;
    }

    std::string rest = symbol.substr(pos);
    // Slash bass: last '/' followed by a note name at the end of the symbol.
    std::size_t slash = rest.rfind('/');
    if (slash != std::string::npos) {
        int bass = pitchClassFromName(rest.substr(slash + 1));
        if (bass >= 0) {
            c.bass = bass;
            rest = rest.substr(0, slash);
        } else {
            result.warning = "unrecognised slash bass in \"" + symbolIn + "\"";
            rest = rest.substr(0, slash);
        }
    }

    enum class Quality { Major, Minor, Dim, HalfDim, Aug } quality = Quality::Major;
    bool majorSeventh = false;
    std::size_t p = 0;
    auto consumeMaj = [&]() {
        if (startsWith(rest, p, "maj") || startsWith(rest, p, "Maj") || startsWith(rest, p, "MAJ")) {
            p += 3;
            majorSeventh = true;
            return true;
        }
        const bool maSeventh = p + 2 >= rest.size() || std::isdigit(static_cast<unsigned char>(rest[p + 2])) ||
                               rest[p + 2] == '(';
        if (startsWith(rest, p, "ma") && !startsWith(rest, p, "maj") && maSeventh) {
            p += 2;
            majorSeventh = true;
            return true;
        }
        if (startsWith(rest, p, "M")) {
            p += 1;
            majorSeventh = true;
            return true;
        }
        return false;
    };

    if (consumeMaj()) {
        quality = Quality::Major;
    } else if (startsWith(rest, p, "min") || startsWith(rest, p, "mi")) {
        p += startsWith(rest, p, "min") ? 3 : 2;
        quality = Quality::Minor;
    } else if (startsWith(rest, p, "m") || startsWith(rest, p, "-")) {
        p += 1;
        quality = Quality::Minor;
    } else if (startsWith(rest, p, "dim")) {
        p += 3;
        quality = Quality::Dim;
    } else if (startsWith(rest, p, "o")) {
        p += 1;
        quality = Quality::Dim;
    } else if (startsWith(rest, p, "h")) {
        p += 1;
        quality = Quality::HalfDim;
    } else if (startsWith(rest, p, "aug")) {
        p += 3;
        quality = Quality::Aug;
    } else if (startsWith(rest, p, "+")) {
        p += 1;
        quality = Quality::Aug;
    }
    // Minor-major seventh: "mMaj7", "m(maj7)", "mM7".
    if (quality == Quality::Minor) {
        if (startsWith(rest, p, "(")) {
            std::size_t save = p;
            ++p;
            if (!consumeMaj()) p = save;
        } else {
            consumeMaj();
        }
    }

    switch (quality) {
        case Quality::Major: c.third = 4; c.fifth = 7; break;
        case Quality::Minor: c.third = 3; c.fifth = 7; break;
        case Quality::Dim: c.third = 3; c.fifth = 6; break;
        case Quality::HalfDim: c.third = 3; c.fifth = 6; c.seventh = 10; break;
        case Quality::Aug: c.third = 4; c.fifth = 8; break;
    }

    // Main extension number.
    int ext = 0;
    if (p < rest.size() && std::isdigit(static_cast<unsigned char>(rest[p]))) {
        while (p < rest.size() && std::isdigit(static_cast<unsigned char>(rest[p]))) ext = ext * 10 + (rest[p++] - '0');
    }
    auto seventhInterval = [&]() {
        if (majorSeventh) return 11;
        if (quality == Quality::Dim) return 9;
        return 10;
    };
    bool understood = true;
    switch (ext) {
        case 0:
            if (quality == Quality::HalfDim) c.seventh = 10;
            break;
        case 2: c.third = 2; c.sus = true; break;  // C2 = Csus2
        case 4: c.third = 5; c.sus = true; break;  // C4 = Csus4
        case 5:
            if (quality == Quality::Major && !majorSeventh) {
                c.power = true;
                c.third = -1;
            } else {
                understood = false;
            }
            break;
        case 6: c.sixth = 9; break;
        case 69: c.sixth = 9; addTension(c, 14); break;
        case 7: c.seventh = seventhInterval(); break;
        case 9: c.seventh = seventhInterval(); addTension(c, 14); break;
        case 11:
            c.seventh = seventhInterval();
            addTension(c, 14);
            if (quality == Quality::Minor || quality == Quality::HalfDim || quality == Quality::Dim) {
                addTension(c, 17);
            } else {
                // Dominant/major 11: the natural 11 replaces the 3rd (voiced as sus4).
                c.third = 5;
                c.sus = true;
            }
            break;
        case 13:
            c.seventh = seventhInterval();
            addTension(c, 14);
            addTension(c, 21);
            if (quality == Quality::Minor) addTension(c, 17);
            break;
        default: understood = false; break;
    }
    if (majorSeventh && ext == 0) {
        // "Cmaj" / "CM" alone is a major triad.
        majorSeventh = false;
    }

    // Modifiers.
    while (p < rest.size() && understood) {
        char ch = rest[p];
        if (ch == '(' || ch == ')' || ch == ',') {
            ++p;
            continue;
        }
        if (startsWith(rest, p, "sus")) {
            p += 3;
            int n = 4;
            if (p < rest.size() && (rest[p] == '2' || rest[p] == '4')) n = rest[p++] - '0';
            c.third = n == 2 ? 2 : 5;
            c.sus = true;
            continue;
        }
        if (startsWith(rest, p, "add")) {
            p += 3;
            int acc = 0, n = 0;
            if (!readAlteration(rest, p, acc, n)) {
                understood = false;
                break;
            }
            int semis = 0;
            switch (n) {
                case 2: case 9: semis = 14; break;
                case 4: case 11: semis = 17; break;
                case 6: case 13: semis = 21; break;
                default: understood = false; break;
            }
            if (!understood) break;
            addTension(c, semis + acc);
            continue;
        }
        if (startsWith(rest, p, "maj7") || startsWith(rest, p, "Maj7") || startsWith(rest, p, "M7")) {
            p += rest[p] == 'M' && rest[p + 1] == '7' ? 2 : 4;
            c.seventh = 11;
            continue;
        }
        if (startsWith(rest, p, "alt")) {
            p += 3;
            if (c.seventh < 0) c.seventh = 10;
            c.fifth = -1;
            removeTension(c, 14);
            removeTension(c, 21);
            addTension(c, 13);
            addTension(c, 15);
            addTension(c, 20);
            continue;
        }
        if (startsWith(rest, p, "no3") || startsWith(rest, p, "omit3")) {
            p += rest[p] == 'n' ? 3 : 5;
            c.third = -1;
            continue;
        }
        if (startsWith(rest, p, "no5") || startsWith(rest, p, "omit5")) {
            p += rest[p] == 'n' ? 3 : 5;
            c.fifth = -1;
            continue;
        }
        int acc = 0, n = 0;
        if (!readAlteration(rest, p, acc, n)) {
            understood = false;
            break;
        }
        switch (n) {
            case 5:
                if (acc == 0) {
                    understood = false;
                } else {
                    c.fifth = 7 + acc;
                }
                break;
            case 6:
                c.sixth = 9 + acc;
                break;
            case 7:
                c.seventh = seventhInterval();
                break;
            case 9:
                if (c.seventh < 0 && c.sixth < 0) c.seventh = seventhInterval();
                removeTension(c, 14);
                addTension(c, 14 + acc);
                break;
            case 11:
                if (c.seventh < 0) c.seventh = seventhInterval();
                if (acc == 0 && c.third == 4) {
                    c.third = 5;
                    c.sus = true;
                } else {
                    removeTension(c, 17);
                    addTension(c, 17 + acc);
                    if (acc > 0 && c.sus && c.third == 5 && ext == 11) {
                        // "C11#11" style oddities: keep the major third.
                        c.third = 4;
                        c.sus = false;
                    }
                }
                break;
            case 13:
                if (c.seventh < 0) c.seventh = seventhInterval();
                removeTension(c, 21);
                addTension(c, 21 + acc);
                if (acc < 0 && c.fifth == 8) c.fifth = 7;
                break;
            default:
                understood = false;
                break;
        }
    }
    if (!understood) {
        result.warning = "partly unrecognised chord symbol \"" + symbolIn + "\"; using \"" +
                         symbol.substr(0, pos) + rest.substr(0, p) + "\"";
    }
    // #5 and b13 coexist as the same pitch class; keep the tension only.
    std::sort(c.tensions.begin(), c.tensions.end());
    if (c.bass == c.root) c.bass = -1;
    result.chord = c;
    return result;
}

}  // namespace flowstate
