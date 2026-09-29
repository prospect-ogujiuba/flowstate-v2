#include "flowstate/smf.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace flowstate {

namespace {

class TrackWriter {
public:
    void varLen(std::uint32_t v) {
        std::uint8_t buf[5];
        int n = 0;
        buf[n++] = static_cast<std::uint8_t>(v & 0x7F);
        while ((v >>= 7) != 0) buf[n++] = static_cast<std::uint8_t>((v & 0x7F) | 0x80);
        while (n > 0) data.push_back(buf[--n]);
    }
    void delta(Tick t) {
        Tick d = std::max<Tick>(0, t - last_);
        varLen(static_cast<std::uint32_t>(d));
        last_ = std::max(last_, t);
    }
    void meta(Tick t, std::uint8_t type, const std::vector<std::uint8_t>& payload) {
        delta(t);
        data.push_back(0xFF);
        data.push_back(type);
        varLen(static_cast<std::uint32_t>(payload.size()));
        for (std::uint8_t byte : payload) data.push_back(byte);
    }
    void text(Tick t, std::uint8_t type, const std::string& s) {
        meta(t, type, std::vector<std::uint8_t>(s.begin(), s.end()));
    }
    void event(Tick t, std::uint8_t status, std::uint8_t a, std::uint8_t b) {
        delta(t);
        data.push_back(status);
        data.push_back(a);
        data.push_back(b);
    }
    void end(Tick t) { meta(t, 0x2F, {}); }

    std::vector<std::uint8_t> data;

private:
    Tick last_ = 0;
};

void be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>((v >> s) & 0xFF));
}
void be16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void appendTrack(std::vector<std::uint8_t>& out, const TrackWriter& t) {
    out.insert(out.end(), {'M', 'T', 'r', 'k'});
    be32(out, static_cast<std::uint32_t>(t.data.size()));
    out.insert(out.end(), t.data.begin(), t.data.end());
}

}  // namespace

std::vector<std::uint8_t> writeSmf(const Realization& r) {
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'M', 'T', 'h', 'd'});
    be32(out, 6);
    be16(out, 1);
    be16(out, static_cast<std::uint16_t>(r.parts.size() + 1));
    be16(out, static_cast<std::uint16_t>(r.ppq));

    const Tick end = r.clipEnd();
    {
        TrackWriter t;
        if (!r.title.empty()) t.text(0, 0x03, r.title);
        const auto usPerQuarter = static_cast<std::uint32_t>(std::lround(60000000.0 / r.tempo));
        t.meta(0, 0x51, {static_cast<std::uint8_t>(usPerQuarter >> 16), static_cast<std::uint8_t>(usPerQuarter >> 8),
                         static_cast<std::uint8_t>(usPerQuarter)});
        std::uint8_t dd = 0;
        for (int d = r.meterDenominator; d > 1; d >>= 1) ++dd;
        const auto clocks = static_cast<std::uint8_t>(std::max(1, 96 / r.meterDenominator));
        t.meta(0, 0x58, {static_cast<std::uint8_t>(r.meterNumerator), dd, clocks, 8});
        t.end(end);
        appendTrack(out, t);
    }

    for (const auto& part : r.parts) {
        TrackWriter t;
        t.text(0, 0x03, part.name.empty() ? part.id : part.name);
        struct Ev {
            Tick tick;
            int on;  // 0 = off (sorted first), 1 = on
            int pitch;
            int vel;
        };
        std::vector<Ev> evs;
        for (const auto& n : part.notes) {
            evs.push_back({n.tick, 1, n.pitch, n.vel});
            evs.push_back({std::min(n.tick + n.dur, end), 0, n.pitch, 0});
        }
        std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            if (a.on != b.on) return a.on < b.on;
            return a.pitch < b.pitch;
        });
        const auto ch = static_cast<std::uint8_t>(part.channel & 0x0F);
        for (const auto& e : evs) {
            if (e.on) t.event(e.tick, static_cast<std::uint8_t>(0x90 | ch), static_cast<std::uint8_t>(e.pitch & 0x7F),
                              static_cast<std::uint8_t>(std::clamp(e.vel, 1, 127)));
            else t.event(e.tick, static_cast<std::uint8_t>(0x80 | ch), static_cast<std::uint8_t>(e.pitch & 0x7F), 0x40);
        }
        t.end(end);
        appendTrack(out, t);
    }
    return out;
}

}  // namespace flowstate
