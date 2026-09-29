#include "session/Capture.h"

#include <algorithm>
#include <cmath>

namespace flowstate::plugin {

void CaptureHistory::trim(double nowClockPpq, double ppqPerBar) {
    const double oldest = nowClockPpq - kMaxBars * ppqPerBar;
    while (!events_.empty() && events_.front().clockPpq < oldest) events_.pop_front();
}

int CaptureHistory::barsAvailable(double nowClockPpq, double ppqPerBar) const {
    if (ppqPerBar <= 0.0) return 0;
    const auto firstOn = std::find_if(events_.begin(), events_.end(), [](const CapturedEvent& e) { return e.velocity > 0; });
    if (firstOn == events_.end()) return 0;
    const double span = std::max(0.0, nowClockPpq - firstOn->clockPpq);
    return std::clamp(static_cast<int>(std::ceil(span / ppqPerBar)), 1, kMaxBars);
}

std::vector<CapturedEvent> CaptureHistory::lastBars(int bars, double nowClockPpq, double ppqPerBar) const {
    const double from = nowClockPpq - std::clamp(bars, 0, kMaxBars) * ppqPerBar;
    std::vector<CapturedEvent> out;
    for (const auto& e : events_)
        if (e.clockPpq >= from) out.push_back(e);
    return out;
}

}  // namespace flowstate::plugin
