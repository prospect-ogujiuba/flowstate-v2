#include "session/Capture.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace flowstate::plugin {

void CaptureHistory::trim(double nowClockPpq, double ppqPerBar) {
    const double oldest = nowClockPpq - kMaxBars * ppqPerBar;
    while (!events_.empty() && events_.front().clockPpq < oldest) events_.pop_front();
}

double CaptureHistory::playedUntil(double nowClockPpq) const {
    std::map<int, int> held;  // (channel, pitch) -> notes sounding
    for (const auto& e : events_) {
        auto& n = held[e.channel * 128 + e.pitch];
        n = e.velocity > 0 ? n + 1 : std::max(0, n - 1);
    }
    const bool holding = std::any_of(held.begin(), held.end(), [](const auto& h) { return h.second > 0; });
    if (holding || events_.empty()) return nowClockPpq;
    return std::min(nowClockPpq, events_.back().clockPpq);
}

int CaptureHistory::barsAvailable(double nowClockPpq, double ppqPerBar) const {
    if (ppqPerBar <= 0.0) return 0;
    const auto firstOn = std::find_if(events_.begin(), events_.end(), [](const CapturedEvent& e) { return e.velocity > 0; });
    if (firstOn == events_.end()) return 0;
    const double span = std::max(0.0, playedUntil(nowClockPpq) - firstOn->clockPpq);
    return std::clamp(static_cast<int>(std::ceil(span / ppqPerBar - 1e-9)), 1, kMaxBars);
}

std::vector<CapturedEvent> CaptureHistory::lastBars(int bars, double nowClockPpq, double ppqPerBar) const {
    // A little slack, so a note played a hair early on the first beat still counts.
    const double from = playedUntil(nowClockPpq) - std::clamp(bars, 0, kMaxBars) * ppqPerBar - 1.0 / 16.0;
    std::vector<CapturedEvent> out;
    for (const auto& e : events_)
        if (e.clockPpq >= from) out.push_back(e);
    return out;
}

flowstate::MidiFileData capturedNotes(const CaptureWindow& window, double ppqPerBar, double tempo, int meterNumerator,
                                      int meterDenominator) {
    struct Played {
        double on = 0.0, off = 0.0, host = -1.0;
        int channel = 0, pitch = 0, velocity = 0;
    };
    std::vector<Played> played;
    std::map<int, std::vector<std::size_t>> held;  // (channel, pitch) -> open notes, oldest first
    for (const auto& e : window.events) {
        const int key = e.channel * 128 + e.pitch;
        if (e.velocity > 0) {
            held[key].push_back(played.size());
            played.push_back({e.clockPpq, -1.0, e.hostPpq, e.channel, e.pitch, e.velocity});
        } else if (auto it = held.find(key); it != held.end() && !it->second.empty()) {
            played[it->second.front()].off = e.clockPpq;
            it->second.erase(it->second.begin());
        }
    }
    for (auto& p : played)
        if (p.off < 0.0) p.off = std::max(p.on, window.endClockPpq);

    flowstate::MidiFileData data;
    data.sourcePpq = flowstate::kPpq;
    data.tempo = tempo;
    data.meterNumerator = meterNumerator;
    data.meterDenominator = meterDenominator;
    if (played.empty()) return data;

    // The host's clock and the capture clock advance together while the host plays, so a steady
    // offset between them means the host's bar lines can be trusted; a loop or jump breaks it.
    const double offset = played.front().host - played.front().on;
    const bool onHost = ppqPerBar > 0.0 && std::all_of(played.begin(), played.end(), [&](const Played& p) {
        return p.host >= 0.0 && std::abs((p.host - p.on) - offset) < 1.0 / 32.0;
    });
    const double origin = onHost ? std::floor(played.front().host / ppqPerBar + 1e-6) * ppqPerBar - offset : played.front().on;

    for (const auto& p : played) {
        flowstate::MidiNote n;
        n.tick = static_cast<flowstate::Tick>(std::llround(std::max(0.0, p.on - origin) * flowstate::kPpq));
        n.dur = std::max<flowstate::Tick>(1, static_cast<flowstate::Tick>(std::llround((p.off - p.on) * flowstate::kPpq)));
        n.pitch = p.pitch;
        n.vel = p.velocity;
        n.channel = p.channel;
        data.notes.push_back(n);
        data.endTick = std::max(data.endTick, n.tick + n.dur);
    }
    std::stable_sort(data.notes.begin(), data.notes.end(), [](const flowstate::MidiNote& a, const flowstate::MidiNote& b) {
        return a.tick != b.tick ? a.tick < b.tick : a.pitch < b.pitch;
    });
    return data;
}

}  // namespace flowstate::plugin
