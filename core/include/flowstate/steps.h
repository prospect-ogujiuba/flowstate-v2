// Step-string parsing ("x..x|x-.."), shared by all roles.
#pragma once

#include <string>
#include <vector>

namespace flowstate {

// One bar per entry; each entry has exactly `stepsPerBar` tokens.
struct StepPattern {
    int stepsPerBar = 0;
    std::vector<std::string> bars;

    bool empty() const { return bars.empty(); }
    // Token at a block-relative bar (pattern repeats) and step.
    char at(int blockBar, int step) const;
};

// Parses `text`. Spaces are ignored, '|' separates bars. Without '|', the
// string is cut into bars of `stepsPerBar`. Short bars whose length divides
// the bar are tiled; other short bars are padded with rests; long bars are
// truncated. Tokens not in `allowed` become rests. Every repair is reported
// in `warnings` prefixed by `where`.
StepPattern parseSteps(const std::string& text, int stepsPerBar, const std::string& allowed,
                       const std::string& where, std::vector<std::string>& warnings);

// A struck step plus its '-' holds. `step` is block-relative and global
// across bars (bar * stepsPerBar + index).
struct StepEvent {
    char token = 'x';
    int step = 0;
    int length = 1;
};

// Expands the pattern over `blockBars` bars. '-' extends the previous event
// (also across bar lines); '-' after a rest is a rest.
std::vector<StepEvent> stepEvents(const StepPattern& pattern, int blockBars);

}  // namespace flowstate
