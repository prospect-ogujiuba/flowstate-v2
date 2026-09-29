#include "flowstate/harmony.h"

#include <algorithm>

namespace flowstate {

Harmony::Harmony(const std::vector<ChordEntry>& harmony, const TimeGrid& time, const Scale& scale,
                 std::vector<std::string>& warnings) {
    struct Pending {
        Tick start, end;
        Chord chord;
        std::size_t order;
    };
    std::vector<Pending> pending;
    for (std::size_t i = 0; i < harmony.size(); ++i) {
        const auto& e = harmony[i];
        const std::string where = "harmony[" + std::to_string(i) + "] \"" + e.symbol + "\"";
        auto parsed = parseChord(e.symbol);
        if (!parsed.warning.empty()) warnings.push_back(where + ": " + parsed.warning);
        if (!parsed.chord) {
            warnings.push_back(where + ": skipped (bars rest)");
            continue;
        }
        if (e.beat < 1.0 || e.beat >= time.numerator() + 1.0)
            warnings.push_back(where + ": beat " + std::to_string(e.beat) + " outside the bar");
        Tick start = time.at(e.bar, e.beat);
        Tick end = start + time.beatsToTicks(e.beats);
        if (e.beats <= 0.0 || end <= start) {
            warnings.push_back(where + ": non-positive duration; skipped");
            continue;
        }
        if (start >= time.clipEnd() || end <= 0) {
            warnings.push_back(where + ": outside the clip; skipped");
            continue;
        }
        pending.push_back({std::max<Tick>(start, 0), std::min(end, time.clipEnd()), *parsed.chord, i});
    }
    std::stable_sort(pending.begin(), pending.end(),
                     [](const Pending& a, const Pending& b) { return a.start < b.start; });
    for (std::size_t i = 0; i < pending.size(); ++i) {
        auto& cur = pending[i];
        if (!spans_.empty() && spans_.back().end > cur.start) {
            auto& prev = spans_.back();
            warnings.push_back("harmony: \"" + prev.chord.symbol + "\" overlaps \"" + cur.chord.symbol +
                               "\"; later chord wins");
            if (prev.start == cur.start) {
                // Same onset: the one listed later in the IR wins.
                if (cur.order > static_cast<std::size_t>(prev.index)) spans_.pop_back();
                else continue;
            } else {
                prev.end = cur.start;
            }
        }
        ChordSpan span;
        span.start = cur.start;
        span.end = cur.end;
        span.chord = cur.chord;
        span.index = static_cast<int>(cur.order);
        spans_.push_back(span);
    }
    // Re-index sequentially after trimming (index = timeline position).
    for (std::size_t i = 0; i < spans_.size(); ++i) spans_[i].index = static_cast<int>(i);

    if (spans_.empty()) {
        implicit_ = true;
        warnings.push_back("harmony: empty; using the tonic chord of the mode for the whole clip");
        ChordSpan span;
        span.start = 0;
        span.end = time.clipEnd();
        span.chord = diatonicChord(scale, 1, false);
        spans_.push_back(span);
    }
}

const ChordSpan* Harmony::at(Tick t) const {
    auto it = std::upper_bound(spans_.begin(), spans_.end(), t,
                               [](Tick v, const ChordSpan& s) { return v < s.start; });
    if (it == spans_.begin()) return nullptr;
    --it;
    return (t >= it->start && t < it->end) ? &*it : nullptr;
}

const ChordSpan* Harmony::nextAfter(Tick t) const {
    for (const auto& s : spans_)
        if (s.start > t) return &s;
    return spans_.empty() ? nullptr : &spans_.front();
}

}  // namespace flowstate
