// Round-trips the shared bridge fixtures (schema/fixtures/bridge) through the generated C++ types.
// schema/src/bridge.test.ts does the same through Zod and must agree on every error path.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "flowstate/bridge.h"

#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>

namespace fb = flowstate::bridge;
using fb::json;

namespace {

json loadFixture(const std::string& root) {
    const auto file = std::filesystem::path(FLOWSTATE_BRIDGE_FIXTURES_DIR) / (root + ".json");
    std::ifstream in(file);
    REQUIRE_MESSAGE(in.good(), "missing fixture " << file.string());
    std::stringstream text;
    text << in.rdbuf();
    return json::parse(text.str());
}

template <typename T>
json roundTrip(const json& value) {
    T parsed;
    from_json(value, parsed);
    json out;
    to_json(out, parsed);
    return out;
}

// One entry per bridge root (bridgeRoots in schema/src/bridge.ts).
const std::map<std::string, std::function<json(const json&)>>& roots() {
    static const std::map<std::string, std::function<json(const json&)>> table{
        {"Command", roundTrip<fb::Command>},
        {"Reply", roundTrip<fb::Reply>},
        {"PluginEvent", roundTrip<fb::PluginEvent>},
        {"SavedSession", roundTrip<fb::SavedSession>},
        {"PlanRequest", roundTrip<fb::PlanRequest>},
        {"EditRequest", roundTrip<fb::EditRequest>},
        {"ServiceEvent", roundTrip<fb::ServiceEvent>},
        {"Health", roundTrip<fb::Health>},
    };
    return table;
}

}  // namespace

TEST_CASE("every bridge root has a fixture file and it round-trips") {
    for (const auto& [root, trip] : roots()) {
        const json fixture = loadFixture(root);
        CAPTURE(root);

        for (const auto& value : fixture.at("valid")) {
            CAPTURE(value.dump());
            json out;
            CHECK_NOTHROW(out = trip(value));
            CHECK(out == value);
        }

        for (const auto& bad : fixture.at("invalid")) {
            const std::string why = bad.at("why").get<std::string>();
            const std::string expected = bad.at("path").get<std::string>();
            CAPTURE(why);
            bool threw = false;
            try {
                trip(bad.at("value"));
            } catch (const fb::ParseError& e) {
                threw = true;
                CHECK(e.path() == expected);
            }
            CHECK_MESSAGE(threw, root << " accepted '" << why << "'");
        }
    }
}

TEST_CASE("variant members carry the discriminator on the way out") {
    fb::Command command = fb::Vary{"keys"};
    json j;
    to_json(j, command);
    CHECK(j == json{{"type", "vary"}, {"partId", "keys"}});

    fb::Command back;
    from_json(j, back);
    REQUIRE(std::holds_alternative<fb::Vary>(back));
    CHECK(std::get<fb::Vary>(back).partId == "keys");
}

TEST_CASE("nulls map to empty optionals and back") {
    fb::ContextOverride override;
    override.tempo = 128.0;
    json j;
    to_json(j, override);
    CHECK(j.at("tempo") == 128.0);
    CHECK(j.at("tonic").is_null());

    fb::ContextOverride back;
    from_json(j, back);
    CHECK(back.tempo == 128.0);
    CHECK_FALSE(back.tonic.has_value());
}

TEST_CASE("integers: integral floats accepted, fractions and overflow rejected") {
    fb::ClipNote note;
    from_json(json::parse(R"({"tick": 960.0, "dur": 480, "pitch": 60, "vel": 100})"), note);
    CHECK(note.tick == 960);

    CHECK_THROWS_AS(from_json(json::parse(R"({"tick": 0.5, "dur": 480, "pitch": 60, "vel": 100})"), note), fb::ParseError);
    CHECK_THROWS_AS(from_json(json::parse(R"({"tick": 4294967296, "dur": 480, "pitch": 60, "vel": 100})"), note), fb::ParseError);
}

TEST_CASE("unknown keys are ignored, as Zod strips them") {
    fb::Health health;
    from_json(json::parse(R"({"protocol": "flowstate.bridge.v0", "version": "1", "ok": true, "extra": 1})"), health);
    CHECK(health.ok);
}

TEST_CASE("scores are carried as raw JSON for core to parse") {
    const json saved = loadFixture("SavedSession").at("valid").at(0);
    fb::SavedSession session;
    from_json(saved, session);
    REQUIRE_FALSE(session.nodes.empty());
    CHECK(session.nodes[0].score.at("ir") == "flowstate.score.v0");
    CHECK(session.nodes[0].seed == 4294967295LL);
}
