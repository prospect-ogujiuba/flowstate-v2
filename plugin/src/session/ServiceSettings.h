// Where the plugin finds the agent service, and the tester token for it (docs/bridge-spec.md, "The
// plugin's client"): the environment first, then the user's service.json, then the local dev service.
// JUCE-free; ServiceClient reads the environment and the file and passes them in.
#pragma once

#include <optional>
#include <string>

namespace flowstate::plugin {

struct ServiceSettings {
    std::string url;    // no trailing slash
    std::string token;  // empty: send no Authorization header
};

inline constexpr const char* kLocalServiceUrl = "http://127.0.0.1:8787";

// envUrl/envToken: FLOWSTATE_SERVICE_URL and FLOWSTATE_SERVICE_TOKEN (empty when unset).
// fileJson: the contents of service.json, if it exists: {"url": "...", "token": "..."}. A file that
// isn't valid JSON is ignored, so a broken file falls back to the local service rather than failing.
// The environment wins field by field. The token is dropped unless the URL is https or loopback,
// so it never crosses the network in the clear.
ServiceSettings resolveServiceSettings(const std::string& envUrl, const std::string& envToken,
                                       const std::optional<std::string>& fileJson);

// True for an https URL, or http to 127.0.0.1, ::1 or localhost.
bool tokenAllowedFor(const std::string& url);

}  // namespace flowstate::plugin
