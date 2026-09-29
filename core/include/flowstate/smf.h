// Standard MIDI File (type 1) writer.
#pragma once

#include "flowstate/realize.h"

#include <cstdint>
#include <vector>

namespace flowstate {

// Track 0: title, tempo and time signature. Then one track per part, named
// after the part, on its channel (drums on channel 10 = index 9). Every
// track's end-of-track sits at the clip end. Note-offs sort before note-ons
// at the same tick.
std::vector<std::uint8_t> writeSmf(const Realization& r);

}  // namespace flowstate
