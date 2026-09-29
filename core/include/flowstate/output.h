// JSON outputs: note list and realization report.
#pragma once

#include "flowstate/realize.h"

#include <cstdint>
#include <string>

namespace flowstate {

// {"ppq","bars","ticksPerBar","tempo","meter":[n,d],"parts":[{"id","role",
//  "name","channel" (1-based),"notes":[{"tick","dur","pitch","vel","sublane"?}],
//  "voices"? (drums only: [{"voice","note","sublane"}])}]}
std::string notesJson(const Realization& r, int indent = -1);

// {"warnings":[..],"outOfKey":[{"part","tick","pitch"}],"stats":{...}}
std::string reportJson(const Realization& r, int indent = 2);

// FNV-1a over the canonical note list (stable golden-test checksum).
std::uint64_t notesChecksum(const Realization& r);

}  // namespace flowstate
