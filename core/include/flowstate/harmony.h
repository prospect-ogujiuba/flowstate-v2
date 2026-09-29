// Chord timeline resolved to ticks.
#pragma once

#include "flowstate/theory.h"
#include "flowstate/timing.h"

#include <string>
#include <vector>

namespace flowstate {

struct ChordSpan {
    Tick start = 0;
    Tick end = 0;  // exclusive
    Chord chord;
    int index = 0;  // position in the timeline
};

class Harmony {
public:
    // Parses symbols, sorts by onset, trims overlaps (later chord wins) and
    // clips to the clip. When `harmony` is empty, the whole clip becomes the
    // tonic chord of the mode (reported as a warning).
    Harmony(const std::vector<ChordEntry>& harmony, const TimeGrid& time, const Scale& scale,
            std::vector<std::string>& warnings);

    const std::vector<ChordSpan>& spans() const { return spans_; }
    // Chord sounding at `t`, or nullptr in a gap.
    const ChordSpan* at(Tick t) const;
    // First chord starting strictly after `t`; wraps to the first chord (clips loop).
    const ChordSpan* nextAfter(Tick t) const;
    bool implicit() const { return implicit_; }

private:
    std::vector<ChordSpan> spans_;
    bool implicit_ = false;
};

}  // namespace flowstate
