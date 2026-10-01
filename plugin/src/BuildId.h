#pragma once

namespace flowstate::plugin {

// FLOWSTATE_BUILD_ID from CMake, shown in Settings. It lives in a generated source file of its own
// (BuildId.cpp.in) so a new ID recompiles one file rather than every file in the plugin, which
// keeps CI's compiler cache useful.
extern const char* const kBuildId;

}  // namespace flowstate::plugin
