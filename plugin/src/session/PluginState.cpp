#include "session/PluginState.h"

#include <algorithm>

namespace flowstate::plugin {

std::string encodeState(const PluginState& state) {
    nlohmann::json session;
    bridge::to_json(session, state.session);
    return encodeStateRaw(session.dump(), state.editorWidth, state.editorHeight);
}

std::string encodeStateRaw(const std::string& sessionJson, int editorWidth, int editorHeight) {
    return std::string(R"({"format":")") + kStateFormat + R"(","version":)" + std::to_string(kStateVersion) +
           R"(,"session":)" + sessionJson + R"(,"editor":{"width":)" + std::to_string(editorWidth) +
           R"(,"height":)" + std::to_string(editorHeight) + "}}";
}

std::optional<PluginState> decodeState(const std::string& blob, std::string& error) {
    const auto j = nlohmann::json::parse(blob, nullptr, false);
    if (j.is_discarded() || !j.is_object() || j.value("format", "") != kStateFormat) {
        error = "not a Flowstate state";
        return std::nullopt;
    }
    if (!j.contains("version") || !j["version"].is_number_integer() || j["version"].get<int>() > kStateVersion) {
        error = "state version is newer than this build";
        return std::nullopt;
    }
    PluginState state;
    try {
        bridge::from_json(j.at("session"), state.session);
    } catch (const bridge::ParseError& e) {
        error = std::string("session: ") + e.what();
        return std::nullopt;
    } catch (const nlohmann::json::exception&) {
        error = "session: missing";
        return std::nullopt;
    }
    if (const auto it = j.find("editor"); it != j.end() && it->is_object()) {
        const auto size = [&](const char* key, int fallback, int min) {
            const auto v = it->find(key);
            return v != it->end() && v->is_number_integer() ? std::clamp(v->get<int>(), min, 8192) : fallback;
        };
        state.editorWidth = size("width", 960, 720);
        state.editorHeight = size("height", 600, 480);
    }
    return state;
}

}  // namespace flowstate::plugin
