// Instant sketch (P1-10): a rule-based score IR from the session context alone, with no model call, so
// every Generate makes sound at once. AI parts replace sketch parts as they stream in.
//
// The sketch is ordinary IR: a diatonic progression for the mode, chord, bass and melody rhythms, and a named
// groove that fits the style and meter. The seed picks among the templates, so sketches vary; the same request
// and seed always give the same score.
#pragma once

#include "flowstate/ir.h"

#include <cstdint>
#include <string>
#include <vector>

namespace flowstate {

struct SketchRequest {
    Context context;          // tonic, mode, meter, tempo, bars, swing and style tags
    std::vector<Role> roles;  // parts to write, in order; empty = chords, bass, melody, drums
    std::uint64_t seed = 1;
};

// The sketch as score IR JSON (flowstate.score.v0). Its part ids are "sketch-<role>"; its title is "Sketch".
// Valid for every mode, meter and length the IR accepts.
std::string sketchJson(const SketchRequest& request);

}  // namespace flowstate
