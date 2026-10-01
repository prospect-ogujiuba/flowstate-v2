#include <doctest/doctest.h>

#include "flowstate/grooves.h"
#include "flowstate/realize.h"
#include "flowstate/steps.h"

#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using namespace flowstate;
using nlohmann::json;

namespace {

json score(int num, int den, int bars, json drumsBlock) {
    json b = {{"startBar", 1}, {"endBar", bars}};
    for (auto& [k, v] : drumsBlock.items()) b[k] = v;
    return json{{"ir", "flowstate.score.v0"}, {"title", "t"},
                {"context", {{"tempo", 100}, {"meterNumerator", num}, {"meterDenominator", den}, {"tonic", "C"},
                             {"mode", "major"}, {"bars", bars}, {"swing", 0}, {"style", json::array()}}},
                {"form", json::array()}, {"harmony", json::array()}, {"motifs", json::array()},
                {"parts", json::array({json{{"id", "kit"}, {"role", "drums"}, {"name", "kit"}, {"low", "C1"},
                                            {"high", "C6"}, {"grid", 4}, {"velocity", 100}, {"blocks", json::array({b})}}})}};
}

Realization run(const json& s) {
    RealizeOptions o;
    o.seed = 1;
    o.humanize = false;
    return realizeJson(s.dump(), o);
}

// Onset ticks per GM note.
std::map<int, std::vector<Tick>> onsets(const Realization& r) {
    std::map<int, std::vector<Tick>> out;
    for (const auto& n : r.parts[0].notes) out[n.pitch].push_back(n.tick);
    return out;
}

}  // namespace

TEST_CASE("grooves: core's library matches the IR contract") {
    std::ifstream in(FLOWSTATE_SCORE_SCHEMA);
    REQUIRE(in.good());
    std::stringstream ss;
    ss << in.rdbuf();
    const json schema = json::parse(ss.str());
    // Find the groove enum's "grooves" table wherever the generator put it.
    const json* table = nullptr;
    std::function<void(const json&)> walk = [&](const json& j) {
        if (table) return;
        if (j.is_object()) {
            if (j.contains("grooves") && j.at("grooves").is_object()) table = &j.at("grooves");
            for (const auto& [k, v] : j.items()) walk(v);
        } else if (j.is_array()) {
            for (const auto& v : j) walk(v);
        }
    };
    walk(schema);
    REQUIRE(table != nullptr);
    std::set<std::string> contract, ours;
    for (const auto& [name, g] : table->items()) {
        contract.insert(name);
        const Groove* mine = findGroove(name);
        REQUIRE_MESSAGE(mine != nullptr, name);
        CHECK_MESSAGE(mine->numerator == g.at("meter").at(0).get<int>(), name);
        CHECK_MESSAGE(mine->denominator == g.at("meter").at(1).get<int>(), name);
    }
    for (const auto& g : grooves()) ours.insert(g.name);
    CHECK(ours == contract);
}

TEST_CASE("grooves: every lane is well formed: bars of a whole resolution, known tokens") {
    for (const auto& g : grooves()) {
        for (const auto& lane : g.lanes) {
            CAPTURE(g.name);
            CAPTURE(toString(lane.voice));
            std::vector<std::string> w;
            const int perBar = g.numerator * g.grid;
            StepPattern p = parseSteps(lane.steps, perBar, "xXg.", "t", w);
            CHECK(!p.empty());
            for (const auto& msg : w) CHECK_MESSAGE(msg.find("read as") != std::string::npos, msg);
        }
    }
}

TEST_CASE("grooves: every groove realizes cleanly in its meter") {
    for (const auto& g : grooves()) {
        CAPTURE(g.name);
        Realization r = run(score(g.numerator, g.denominator, 2, json{{"groove", g.name}}));
        CHECK(!r.parts[0].notes.empty());
        for (const auto& w : r.warnings) CHECK_MESSAGE(w.find("groove") == std::string::npos, w);
    }
}

TEST_CASE("grooves: dembow places its kicks and snares; block lanes only add voices the groove lacks") {
    auto hits = onsets(run(score(4, 4, 1, json{{"groove", "dembow"}})));
    CHECK(hits[36] == std::vector<Tick>{0, 960, 1920, 2880});
    CHECK(hits[38] == std::vector<Tick>{720, 1440, 2640, 3360});  // 16th steps 3, 6, 11, 14
    CHECK(hits[42].size() == 8);

    json own = {{"groove", "dembow"},
                {"drums", json::array({json{{"voice", "kick"}, {"steps", "x..............."}},
                                       json{{"voice", "cowbell"}, {"steps", "x...x...x...x..."}}})}};
    Realization r = run(score(4, 4, 1, own));
    auto mixed = onsets(r);
    CHECK(mixed[36] == hits[36]);  // the groove's kick, not the block's
    CHECK(mixed[56].size() == 4);  // the added cowbell plays
    bool warned = false;
    for (const auto& w : r.warnings) warned = warned || w.find("kick lane ignored") != std::string::npos;
    CHECK(warned);
}

TEST_CASE("grooves: feel sits a lane behind the grid") {
    auto straight = onsets(run(score(4, 4, 1, json{{"groove", "boom_bap"}})));
    auto lazy = onsets(run(score(4, 4, 1, json{{"groove", "lofi"}})));
    // boom_bap's snare is 0.12 of a 16th late (29 ticks), lofi's 0.2 (48 ticks); the kick is on the grid.
    CHECK(straight[38].front() == 960 + 29);
    CHECK(lazy[38].front() == 960 + 48);
    CHECK(lazy[36].front() == 0);
}

TEST_CASE("grooves: triplet grooves land on the triplet grid") {
    auto hits = onsets(run(score(4, 4, 1, json{{"groove", "jazz_swing"}})));
    // Spang-a-lang ride: beats 1 and 3, beat 2 and 4 with the triplet "let" after them.
    CHECK(hits[51] == std::vector<Tick>{0, 960, 1600, 1920, 2880, 3520});
    CHECK(hits[44] == std::vector<Tick>{960, 2880});
}

TEST_CASE("grooves: an unknown groove or one for another meter is ignored with a warning") {
    Realization unknown = run(score(4, 4, 1, json{{"groove", "polka"}}));
    Realization meter = run(score(4, 4, 1, json{{"groove", "jazz_waltz"}}));
    auto warned = [](const Realization& r, const char* what) {
        for (const auto& w : r.warnings)
            if (w.find(what) != std::string::npos) return true;
        return false;
    };
    CHECK(warned(unknown, "unknown groove"));
    CHECK(warned(meter, "is for 3/4"));
    CHECK(meter.parts[0].notes.empty());
}
