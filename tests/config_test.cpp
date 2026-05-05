#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "neurostream/config.hpp"
#include <filesystem>
#include <string>

#ifndef NS_TEST_DATA_DIR
#error "NS_TEST_DATA_DIR must be defined"
#endif

using namespace neurostream;

static std::filesystem::path data_path(const char* name) {
    return std::filesystem::path(NS_TEST_DATA_DIR) / name;
}

TEST_CASE("loads a valid config and parses every field") {
    auto cfg = load_config(data_path("valid_config.yaml"));

    CHECK(cfg.ssd.raw_bandwidth_mbps == 5500);
    CHECK(cfg.ssd.effective_bandwidth_mbps == 8500);
    CHECK(cfg.ssd.decompress_ratio == doctest::Approx(2.0));
    CHECK(cfg.ssd.decompress_latency_us == 5);

    CHECK(cfg.bus.total_bandwidth_mbps == 16000);
    CHECK(cfg.bus.transaction_overhead_us == 1);

    CHECK(cfg.npu.cores == 4);
    CHECK(cfg.npu.shared_cache_mb == 512);

    CHECK(cfg.audio.priority == "critical");
    CHECK(cfg.audio.deadline_us == 1000);

    CHECK(cfg.scheduler.policy == "qos");
    CHECK(cfg.scheduler.qos_weights.critical == 8);
    CHECK(cfg.dma.path == "p2p");
    CHECK(cfg.eviction.policy == "distance_lru");
    CHECK(cfg.degradation.weight_load_timeout_ms == 50);
}

TEST_CASE("missing field triggers ConfigError naming the field path") {
    try {
        load_config(data_path("missing_field_config.yaml"));
        FAIL("expected ConfigError");
    } catch (const ConfigError& e) {
        std::string msg = e.what();
        CHECK(msg.find("ssd.decompress_latency_us") != std::string::npos);
    }
}

TEST_CASE("missing file triggers ConfigError") {
    CHECK_THROWS_AS(load_config(data_path("does_not_exist.yaml")), ConfigError);
}
