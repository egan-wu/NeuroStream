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

TEST_CASE("loads valid scenario fully") {
    auto s = load_scenario(data_path("valid_scenario.yaml"));
    CHECK(s.name == "test_basic");
    CHECK(s.duration_ms == 1000);
    CHECK(s.audio_enabled == true);

    REQUIRE(s.texture_bursts.size() == 1);
    CHECK(s.texture_bursts[0].at_ms == 200);
    CHECK(s.texture_bursts[0].rate_mbps == 500);
    CHECK(s.texture_bursts[0].duration_ms == 50);

    REQUIRE(s.npcs.size() == 2);
    CHECK(s.npcs[0].id == 1);
    REQUIRE(s.npcs[0].waypoints.size() == 2);
    CHECK(s.npcs[0].waypoints[0].at_ms == 0);
    CHECK(s.npcs[0].waypoints[0].distance_m == doctest::Approx(80.0));
    CHECK(s.npcs[0].waypoints[1].at_ms == 500);
    CHECK(s.npcs[0].waypoints[1].distance_m == doctest::Approx(5.0));
    CHECK(s.npcs[1].id == 2);
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

TEST_CASE("negative distance_m rejected") {
    CHECK_THROWS_AS(load_scenario(data_path("bad_lod_scenario.yaml")), ScenarioError);
}

TEST_CASE("legacy weight_prefetches field rejected with migration hint") {
    try {
        load_scenario(data_path("legacy_scenario.yaml"));
        FAIL("expected ScenarioError");
    } catch (const ScenarioError& e) {
        std::string msg = e.what();
        CHECK(msg.find("weight_prefetches") != std::string::npos);
        CHECK(msg.find("npcs") != std::string::npos);
    }
}

TEST_CASE("missing file throws ScenarioError") {
    CHECK_THROWS_AS(load_scenario(data_path("nope.yaml")), ScenarioError);
}

TEST_CASE("distance_at: returns endpoints outside range") {
    std::vector<Waypoint> wps = {
        {0,   100.0},
        {500, 10.0},
        {1000, 50.0},
    };
    CHECK(distance_at(wps, -100) == doctest::Approx(100.0));
    CHECK(distance_at(wps, 0)    == doctest::Approx(100.0));
    CHECK(distance_at(wps, 1000) == doctest::Approx(50.0));
    CHECK(distance_at(wps, 9999) == doctest::Approx(50.0));
}

TEST_CASE("distance_at: linear interpolation between waypoints") {
    std::vector<Waypoint> wps = {
        {0,    100.0},
        {1000, 0.0},
    };
    CHECK(distance_at(wps, 250) == doctest::Approx(75.0));
    CHECK(distance_at(wps, 500) == doctest::Approx(50.0));
    CHECK(distance_at(wps, 750) == doctest::Approx(25.0));
}

TEST_CASE("distance_at: empty waypoints returns zero") {
    std::vector<Waypoint> wps;
    CHECK(distance_at(wps, 100) == doctest::Approx(0.0));
}
