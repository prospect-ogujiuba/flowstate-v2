// Named drum grooves: idiomatic patterns per style that a drums block can call by name ("groove": "dembow").
// Each groove is a set of step lanes at its own grid, with per-lane feel (how late the lane sits behind the
// grid), for one meter. The names and meters are part of the IR contract (schema/src/score.ts).
#pragma once

#include "flowstate/ir.h"

#include <string>
#include <vector>

namespace flowstate {

struct GrooveLane {
    DrumVoice voice;
    std::string steps;  // step string at the groove's grid; '|' separates bars, the pattern repeats
    double late = 0.0;  // feel: fraction of one step the lane sits behind the grid (0.2 = lazy)
};

struct Groove {
    std::string name;
    int numerator = 4;
    int denominator = 4;
    int grid = 4;  // steps per beat
    std::vector<GrooveLane> lanes;
};

// Every groove, in a stable order.
const std::vector<Groove>& grooves();
// The groove with this name, or nullptr.
const Groove* findGroove(const std::string& name);

}  // namespace flowstate
