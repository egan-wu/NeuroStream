#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "neurostream/scenario.hpp"
#include <filesystem>
#include <string>

#ifndef NS_TEST_DATA_DIR
#error "NS_TEST_DATA_DIR must be defined"
#endif

using namespace neurostream;

static std::filesystem::path data_path(const char* name) {
    return std::filesystem::path(NS_TEST_DATA_DIR) / name;
}

TEST_CASE("loads valid v3 scenario fully") {
    auto s = load_scenario(data_path("valid_scenario.yaml"));
    CHECK(s.schema_version == 3);
    CHECK(s.name == "test_basic");
    CHECK(s.duration_ms == 1000);
    CHECK(s.audio_enabled == true);

    REQUIRE(s.player.waypoints.size() == 2);
    CHECK(s.player.waypoints[0].pos.x == doctest::Approx(0.0));
    CHECK(s.player.waypoints[1].pos.x == doctest::Approx(5.0));
    CHECK(*s.player.waypoints[0].facing_deg == doctest::Approx(0.0));

    REQUIRE(s.texture_bursts.size() == 1);
    CHECK(s.texture_bursts[0].at_ms == 200);

    REQUIRE(s.npcs.size() == 2);
    CHECK(s.npcs[0].id == 1);
    CHECK(s.npcs[0].priority == "normal");
    CHECK(s.npcs[0].waypoints[0].pos.x == doctest::Approx(80.0));
    CHECK(s.npcs[1].priority == "quest");

    REQUIRE(s.interactions.size() == 1);
    CHECK(s.interactions[0].at_ms == 300);
    CHECK(s.interactions[0].npc_id == 1);
    CHECK(s.interactions[0].duration_ms == 200);
}

TEST_CASE("missing required field throws ScenarioError naming the field") {
    try {
        load_scenario(data_path("missing_field_scenario.yaml"));
        FAIL("expected ScenarioError");
    } catch (const ScenarioError& e) {
        std::string msg = e.what();
        CHECK(msg.find("duration_ms") != std::string::npos);
    }
}

TEST_CASE("invalid priority rejected") {
    CHECK_THROWS_AS(load_scenario(data_path("bad_lod_scenario.yaml")), ScenarioError);
}

TEST_CASE("Phase 2 (weight_prefetches) schema rejected with migration hint") {
    try {
        load_scenario(data_path("legacy_scenario.yaml"));
        FAIL("expected ScenarioError");
    } catch (const ScenarioError& e) {
        std::string msg = e.what();
        CHECK(msg.find("weight_prefetches") != std::string::npos);
        CHECK(msg.find("v3") != std::string::npos);
    }
}

TEST_CASE("Phase 4 (distance_m) schema rejected with migration hint") {
    try {
        load_scenario(data_path("legacy_v2_scenario.yaml"));
        FAIL("expected ScenarioError");
    } catch (const ScenarioError& e) {
        std::string msg = e.what();
        CHECK(msg.find("distance_m") != std::string::npos);
        CHECK(msg.find("pos") != std::string::npos);
    }
}

TEST_CASE("missing file throws ScenarioError") {
    CHECK_THROWS_AS(load_scenario(data_path("nope.yaml")), ScenarioError);
}

TEST_CASE("position_at: endpoint behavior outside the timeline") {
    std::vector<Waypoint> wps = {
        {0,    {0.0, 0.0}, {}},
        {1000, {10.0, 0.0}, {}},
    };
    CHECK(position_at(wps, -100).x == doctest::Approx(0.0));
    CHECK(position_at(wps, 2000).x == doctest::Approx(10.0));
}

TEST_CASE("position_at: linear interpolation between waypoints") {
    std::vector<Waypoint> wps = {
        {0,    {0.0, 0.0}, {}},
        {1000, {100.0, 50.0}, {}},
    };
    CHECK(position_at(wps, 250).x == doctest::Approx(25.0));
    CHECK(position_at(wps, 250).y == doctest::Approx(12.5));
}

TEST_CASE("velocity_at: slope between waypoints") {
    std::vector<Waypoint> wps = {
        {0,    {0.0, 0.0}, {}},
        {1000, {100.0, 50.0}, {}},
    };
    auto v = velocity_at(wps, 500);
    CHECK(v.x == doctest::Approx(0.1));   // 100 m / 1000 ms = 0.1 m/ms
    CHECK(v.y == doctest::Approx(0.05));
}

TEST_CASE("velocity_at: single waypoint returns zero") {
    std::vector<Waypoint> wps = {
        {0, {5.0, 5.0}, {}},
    };
    auto v = velocity_at(wps, 100);
    CHECK(v.x == doctest::Approx(0.0));
    CHECK(v.y == doctest::Approx(0.0));
}

TEST_CASE("facing_at: returns interpolated facing") {
    std::vector<Waypoint> wps = {
        {0,    {0.0, 0.0}, 0.0},
        {1000, {1.0, 0.0}, 90.0},
    };
    CHECK(facing_at(wps, 500) == doctest::Approx(45.0));
}
