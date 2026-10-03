#include <doctest/doctest.h>

#include "flowstate/realize.h"
#include "flowstate/sketch.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using namespace flowstate;

namespace {

SketchRequest request(const char* tonic, Mode mode, int num, int den, int bars, std::uint64_t seed, double tempo = 100.0) {
    SketchRequest r;
    r.context.tonic = tonic;
    r.context.mode = mode;
    r.context.meterNumerator = num;
    r.context.meterDenominator = den;
    r.context.bars = bars;
    r.context.tempo = tempo;
    r.seed = seed;
    return r;
}

// A compact fingerprint of what a sketch plays.
std::string fingerprint(const Realization& r) {
    std::string s;
    for (const auto& p : r.parts)
        for (const auto& n : p.notes) s += std::to_string(n.tick) + ":" + std::to_string(n.pitch) + ",";
    return s;
}

}  // namespace

TEST_CASE("sketch: valid, warning-free IR for every mode, meter and length") {
    const Mode modes[] = {Mode::Major, Mode::Minor, Mode::Dorian, Mode::Phrygian, Mode::Lydian, Mode::Mixolydian, Mode::Locrian,
                          Mode::HarmonicMinor, Mode::MelodicMinor, Mode::MajorPentatonic, Mode::MinorPentatonic, Mode::Blues};
    const std::pair<int, int> meters[] = {{4, 4}, {3, 4}, {6, 8}, {7, 8}, {5, 4}, {12, 8}, {2, 4}};
    const char* tonics[] = {"C", "F#", "Eb", "A"};
    int checked = 0;
    for (const Mode mode : modes)
        for (const auto& [num, den] : meters)
            for (const int bars : {1, 2, 4, 8, 16})
                for (std::uint64_t seed = 1; seed <= 3; ++seed) {
                    const auto req = request(tonics[(seed + bars) % 4], mode, num, den, bars, seed);
                    const auto text = sketchJson(req);
                    INFO(toString(mode) << " " << num << "/" << den << " " << bars << " bars, seed " << seed << ": " << text);
                    Realization r;
                    REQUIRE_NOTHROW(r = realizeJson(text, {seed, true}));
                    CHECK(r.warnings.empty());
                    CHECK(r.bars == bars);
                    CHECK(r.parts.size() == 4);
                    for (const auto& p : r.parts) CHECK_FALSE(p.notes.empty());
                    // In key, except the blues' dominant sevenths, which colour the scale on purpose.
                    if (mode != Mode::Blues) CHECK(r.outOfKey.empty());
                    ++checked;
                }
    CHECK(checked == 12 * 7 * 5 * 3);
}

TEST_CASE("sketch: under 100 ms for 8 bars x 4 parts, sketch and realization together") {
    const auto req = request("A", Mode::Minor, 4, 4, 8, 7);
    std::vector<double> ms;
    for (int i = 0; i < 15; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = realizeJson(sketchJson(req), {static_cast<std::uint64_t>(i + 1), true});
        ms.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
        REQUIRE(r.parts.size() == 4);
    }
    std::sort(ms.begin(), ms.end());
    MESSAGE("sketch + realize, 8 bars x 4 parts: median " << ms[ms.size() / 2] << " ms, max " << ms.back() << " ms");
    CHECK(ms[ms.size() / 2] < 100.0);
}

TEST_CASE("sketch: varied across seeds, identical for the same seed") {
    std::set<std::string> distinct;
    std::set<std::string> harmonies;
    for (std::uint64_t seed = 1; seed <= 16; ++seed) {
        const auto text = sketchJson(request("D", Mode::Minor, 4, 4, 4, seed));
        CHECK(text == sketchJson(request("D", Mode::Minor, 4, 4, 4, seed)));
        distinct.insert(fingerprint(realizeJson(text, {seed, true})));
        harmonies.insert(nlohmann::json::parse(text)["harmony"].get<std::string>());
    }
    CHECK(distinct.size() >= 14);
    CHECK(harmonies.size() >= 4);
}

TEST_CASE("sketch: follows the context: key, groove by style and meter, and the requested roles") {
    auto req = request("Eb", Mode::Major, 4, 4, 4, 3, 140.0);
    req.context.style = {"trap"};
    auto s = nlohmann::json::parse(sketchJson(req));
    CHECK(s["context"]["tonic"] == "Eb");
    CHECK(s["harmony"].get<std::string>().find("Eb") != std::string::npos);  // the tonic chord is in every progression
    const auto drumsOf = [](const nlohmann::json& score) {
        for (const auto& p : score["parts"])
            if (p["role"] == "drums") return p["blocks"][0];
        return nlohmann::json();
    };
    CHECK(drumsOf(s)["groove"] == "trap");

    // A style whose groove is in another meter falls back to one that fits.
    req = request("G", Mode::Major, 6, 8, 4, 2);
    req.context.style = {"house"};
    s = nlohmann::json::parse(sketchJson(req));
    const auto groove = drumsOf(s)["groove"].get<std::string>();
    CHECK((groove == "six_eight" || groove == "gospel_shuffle"));

    // A meter with no groove writes lanes.
    s = nlohmann::json::parse(sketchJson(request("C", Mode::Dorian, 7, 8, 4, 1)));
    CHECK(drumsOf(s).contains("drums"));
    CHECK_FALSE(drumsOf(s).contains("groove"));

    req = request("C", Mode::Major, 4, 4, 4, 1);
    req.roles = {Role::Pad, Role::Arp, Role::Counter};
    s = nlohmann::json::parse(sketchJson(req));
    std::vector<std::string> roles;
    for (const auto& p : s["parts"]) roles.push_back(p["role"]);
    CHECK(roles == std::vector<std::string>{"pad", "arp", "counter"});
    CHECK(s["parts"][0]["id"] == "sketch-pad");
    const auto r = realizeJson(s.dump(), {1, true});
    CHECK(r.warnings.empty());
}
