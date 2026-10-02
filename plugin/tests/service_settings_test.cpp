// The plugin's service URL and tester token (session/ServiceSettings.h).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "session/ServiceSettings.h"

using flowstate::plugin::resolveServiceSettings;
using flowstate::plugin::tokenAllowedFor;

TEST_CASE("no environment and no file: the local dev service, no token") {
    const auto s = resolveServiceSettings("", "", std::nullopt);
    CHECK(s.url == "http://127.0.0.1:8787");
    CHECK(s.token.empty());
}

TEST_CASE("service.json gives the hosted URL and token; the environment wins field by field") {
    const std::string file = R"({"url": "https://flowstate.example.com/", "token": "fst_file"})";
    auto s = resolveServiceSettings("", "", file);
    CHECK(s.url == "https://flowstate.example.com");
    CHECK(s.token == "fst_file");

    s = resolveServiceSettings("", " fst_env ", file);
    CHECK(s.url == "https://flowstate.example.com");
    CHECK(s.token == "fst_env");

    s = resolveServiceSettings("http://127.0.0.1:8787", "", file);
    CHECK(s.url == "http://127.0.0.1:8787");
    CHECK(s.token == "fst_file");
}

TEST_CASE("a broken service.json falls back instead of failing") {
    for (const auto* text : {"", "{", "[]", R"({"url": 5, "token": null})"}) {
        const auto s = resolveServiceSettings("", "", std::string(text));
        CHECK(s.url == "http://127.0.0.1:8787");
        CHECK(s.token.empty());
    }
}

TEST_CASE("the token only goes over https or to this machine") {
    CHECK(tokenAllowedFor("https://flowstate.example.com"));
    CHECK(tokenAllowedFor("HTTPS://Flowstate.example.com/v1"));
    CHECK(tokenAllowedFor("http://127.0.0.1:8787"));
    CHECK(tokenAllowedFor("http://localhost:8787/"));
    CHECK(tokenAllowedFor("http://[::1]:8787"));
    CHECK_FALSE(tokenAllowedFor("http://flowstate.example.com"));
    CHECK_FALSE(tokenAllowedFor("http://127.0.0.1.evil.example:8787"));
    CHECK_FALSE(tokenAllowedFor("http://localhost.evil.example"));
    CHECK_FALSE(tokenAllowedFor("http://user@evil.example"));
    CHECK_FALSE(tokenAllowedFor("ftp://127.0.0.1"));

    const auto s = resolveServiceSettings("http://flowstate.example.com", "fst_x", std::nullopt);
    CHECK(s.url == "http://flowstate.example.com");
    CHECK(s.token.empty());
}
