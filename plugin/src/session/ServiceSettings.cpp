#include "session/ServiceSettings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace flowstate::plugin {

namespace {

std::string trim(std::string s) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

bool tokenAllowedFor(const std::string& url) {
    const auto u = lower(url);
    if (u.rfind("https://", 0) == 0) return true;
    if (u.rfind("http://", 0) != 0) return false;
    auto host = u.substr(7, u.find_first_of("/?#", 7) - 7);
    if (const auto at = host.rfind('@'); at != std::string::npos) host = host.substr(at + 1);
    if (!host.empty() && host.front() == '[') host = host.substr(0, host.find(']') + 1);  // [::1]:8787
    else host = host.substr(0, host.find(':'));
    return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
}

ServiceSettings resolveServiceSettings(const std::string& envUrl, const std::string& envToken,
                                       const std::optional<std::string>& fileJson) {
    std::string fileUrl, fileToken;
    if (fileJson) {
        const auto json = nlohmann::json::parse(*fileJson, nullptr, false);
        if (json.is_object()) {
            if (json.contains("url") && json["url"].is_string()) fileUrl = json["url"].get<std::string>();
            if (json.contains("token") && json["token"].is_string()) fileToken = json["token"].get<std::string>();
        }
    }
    ServiceSettings s;
    s.url = trim(envUrl).empty() ? trim(fileUrl) : trim(envUrl);
    if (s.url.empty()) s.url = kLocalServiceUrl;
    while (!s.url.empty() && s.url.back() == '/') s.url.pop_back();
    s.token = trim(envToken).empty() ? trim(fileToken) : trim(envToken);
    if (!tokenAllowedFor(s.url)) s.token.clear();
    return s;
}

}  // namespace flowstate::plugin
