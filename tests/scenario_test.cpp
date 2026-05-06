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

    REQUIRE(s.weight_prefetches.size() == 2);
    CHECK(s.weight_prefetches[0].at_ms == 100);
    CHECK(s.weight_prefetches[0].npc_id == 1);
    CHECK(s.weight_prefetches[0].lod == 0);
    CHECK(s.weight_prefetches[1].lod == 2);
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

TEST_CASE("invalid LOD value rejected") {
    CHECK_THROWS_AS(load_scenario(data_path("bad_lod_scenario.yaml")), ScenarioError);
}

TEST_CASE("missing file throws ScenarioError") {
    CHECK_THROWS_AS(load_scenario(data_path("nope.yaml")), ScenarioError);
}
