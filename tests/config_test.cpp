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
    CHECK(cfg.scheduler.quantum_us == 100);
    CHECK(cfg.scheduler.critical_rate_limit_pct == 5);
    CHECK(cfg.scheduler.bulk_weights.high == 2);
    CHECK(cfg.scheduler.bulk_weights.normal == 1);

    CHECK(cfg.lod_manager.tick_us == 16667);
    CHECK(cfg.lod_manager.hysteresis_pct == 20);
    REQUIRE(cfg.lod_manager.bands.size() == 3);
    CHECK(cfg.lod_manager.bands[0].lod == 0);
    CHECK(cfg.lod_manager.bands[0].max_distance_m == 10);
    CHECK(cfg.lod_manager.bands[2].max_distance_m == 100);
    CHECK(cfg.predictor.policy == "lod");

    CHECK(cfg.dma.path == "neuro_dma");
    CHECK(cfg.dma.bounce.memcpy_bandwidth_mbps == 12000);
    CHECK(cfg.dma.bounce.cycles_per_byte == 3);
    CHECK(cfg.dma.neuro_dma.sgl_entry_bytes == 1048576);
    CHECK(cfg.dma.neuro_dma.setup_cost_cycles == 1000);
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
