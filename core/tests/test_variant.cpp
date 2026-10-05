#include <doctest/doctest.h>

#include "flowstate/realize.h"
#include "test_util.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using namespace flowstate;
using nlohmann::json;

namespace {

const char* kFixtures[] = {"example.json", "six_eight.json", "seven_eight.json"};

Realization run(const json& score, bool humanize = false, std::uint64_t seed = 9) {
    RealizeOptions o;
    o.seed = seed;
    o.humanize = humanize;
    return realizeJson(score.dump(), o);
}

std::string fingerprint(const PartRealization& p) {
    std::string f;
    for (const auto& n : p.notes)
        f += std::to_string(n.tick) + ',' + std::to_string(n.dur) + ',' + std::to_string(n.pitch) + ',' +
             std::to_string(n.vel) + ';';
    return f;
}

std::size_t outOfKey(const Realization& r, const std::string& id) {
    std::size_t n = 0;
    for (const auto& o : r.outOfKey) n += o.part == id;
    return n;
}

}  // namespace

TEST_CASE("re-roll: a part's own seed is deterministic and leaves every other part as it was") {
    for (const char* name : kFixtures) {
        CAPTURE(name);
        const json base = json::parse(readFixture(name));
        const auto before = run(base, true);
        for (std::size_t i = 0; i < base["parts"].size(); ++i) {
            json rolled = base;
            rolled["parts"][i]["seed"] = 12345;
            const auto a = run(rolled, true), b = run(rolled, true);
            for (std::size_t k = 0; k < a.parts.size(); ++k) {
                CAPTURE(a.parts[k].id);
                CHECK(fingerprint(a.parts[k]) == fingerprint(b.parts[k]));
                if (k != i) CHECK(fingerprint(a.parts[k]) == fingerprint(before.parts[k]));
            }
        }
    }
}

TEST_CASE("re-roll: every part varies across seeds, inside its range, the clip and the key") {
    for (const char* name : kFixtures) {
        const json base = json::parse(readFixture(name));
        const auto written = run(base);
        for (std::size_t i = 0; i < base["parts"].size(); ++i) {
            const std::string id = base["parts"][i]["id"];
            CAPTURE(name);
            CAPTURE(id);
            std::set<std::string> takes;
            for (std::uint64_t seed = 1; seed <= 8; ++seed) {
                json rolled = base;
                rolled["parts"][i]["seed"] = seed * 7919;
                // Without humanize, so only the seeded choices count.
                const auto r = run(rolled);
                const auto& p = r.parts[i];
                takes.insert(fingerprint(p));
                CHECK(outOfKey(r, id) <= outOfKey(written, id));
                CHECK(p.notes.size() > 0);
                for (const auto& n : p.notes) {
                    CHECK(n.tick >= 0);
                    CHECK(n.tick + n.dur <= r.clipEnd());
                    if (p.role != Role::Drums) {
                        CHECK(n.pitch >= p.low);
                        CHECK(n.pitch <= p.high);
                    }
                }
            }
            CHECK(takes.size() >= 6);
        }
    }
}

TEST_CASE("re-roll: an invalid part seed is ignored with a warning") {
    json score = json::parse(readFixture("example.json"));
    score["parts"][0]["seed"] = -3;
    const auto r = realizeJson(score.dump(), {});
    bool warned = false;
    for (const auto& w : r.warnings) warned |= w.find("seed") != std::string::npos;
    CHECK(warned);
    CHECK(fingerprint(r.parts[0]) == fingerprint(realizeJson(readFixture("example.json"), {}).parts[0]));
}
