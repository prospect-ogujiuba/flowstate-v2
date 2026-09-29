// What getStateInformation writes: a small JSON envelope around the bridge's SavedSession.
// {"format":"flowstate.plugin.state","version":1,"session":{SavedSession},"editor":{"width","height"}}
// No keys or credentials ever go in here (docs/bridge-spec.md, P1-12).
#pragma once

#include "flowstate/bridge.h"

#include <optional>
#include <string>
#include <vector>

namespace flowstate::plugin {

inline constexpr const char* kStateFormat = "flowstate.plugin.state";
inline constexpr int kStateVersion = 1;

struct PluginState {
    bridge::SavedSession session;
    int editorWidth = 960;
    int editorHeight = 600;
};

std::string encodeState(const PluginState& state);
// The same envelope around an already-encoded SavedSession (the processor's snapshot).
std::string encodeStateRaw(const std::string& sessionJson, int editorWidth, int editorHeight);

// nullopt (with a reason in `error`) when the blob isn't a Flowstate state we can read: another
// plugin's data, a newer format version, or a session that fails the bridge schema.
std::optional<PluginState> decodeState(const std::string& blob, std::string& error);

}  // namespace flowstate::plugin
