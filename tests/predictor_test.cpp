#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "neurostream/predictor.hpp"
#include "neurostream/transaction.hpp"
#include <vector>

using namespace neurostream;

namespace {

struct RecordingSink : TransactionSink {
    std::vector<Transaction> txns;
    void accept(const Transaction& t) override { txns.push_back(t); }
};

Config make_cfg() {
    Config c{};
    c.ssd.raw_bandwidth_mbps       = 5500;
    c.ssd.effective_bandwidth_mbps = 8500;
    c.ssd.decompress_ratio         = 2.0;
    c.ssd.decompress_latency_us    = 5;
    c.bus.total_bandwidth_mbps     = 16000;
    c.bus.transaction_overhead_us  = 1;
    c.npu.cores                    = 4;
    c.npu.shared_cache_mb          = 512;
    c.audio.rate_mbps              = 5;
    c.audio.packet_bytes           = 256;
    c.audio.deadline_us            = 1000;
    c.audio.priority               = "critical";
    c.texture.burst_rate_mbps      = 500;
    c.texture.burst_duration_ms    = 100;
    c.texture.block_bytes          = 262144;
    c.texture.priority             = "normal";
    c.ai_weights.lod0_mb           = 100;
    c.ai_weights.lod1_mb           = 30;
    c.ai_weights.lod2_mb           = 10;
    c.ai_weights.priority          = "high";
    c.lod_manager.tick_us          = 16667;
    c.lod_manager.hysteresis_pct   = 20;
    c.lod_manager.bands            = {{0, 10}, {1, 30}, {2, 100}};
    c.predictor.policy             = "lod";
    return c;
}

Scenario one_npc(std::uint32_t id, std::vector<Waypoint> wps, int dur_ms) {
    Scenario s;
    s.duration_ms = dur_ms;
    s.npcs.push_back(NpcSpec{id, std::move(wps), "normal"});
    return s;
}

}

TEST_CASE("select_lod: cold start picks conservative LOD inside band") {
    auto cfg = make_cfg();
    LodPredictor p(Scenario{}, cfg);

    // distance 5m → eligible for LOD0/1/2; cold start picks LOD2 (most far).
    CHECK(p.select_lod(5.0,  -2) == 2);
    CHECK(p.select_lod(20.0, -2) == 2);
    CHECK(p.select_lod(50.0, -2) == 2);
    CHECK(p.select_lod(150.0, -2) == -1);     // beyond all bands
}

TEST_CASE("select_lod: upgrades immediately when distance shrinks (no in-hysteresis)") {
    auto cfg = make_cfg();
    LodPredictor p(Scenario{}, cfg);

    // Currently LOD2, distance falls to 8m → should jump to LOD0 (no
    // hysteresis on getting CLOSER, only on getting farther).
    CHECK(p.select_lod(8.0, 2) == 0);
    CHECK(p.select_lod(20.0, 2) == 1);   // 8 ≤ d ≤ 10 not satisfied, 20 ≤ 30 → LOD1
}

TEST_CASE("select_lod: 20% deadband holds on the FAR side") {
    auto cfg = make_cfg();
    LodPredictor p(Scenario{}, cfg);

    // Currently LOD0 (max 10m). leave_far = 10 × 1.2 = 12m.
    CHECK(p.select_lod(10.0, 0) == 0);   // still inside
    CHECK(p.select_lod(11.9, 0) == 0);   // inside deadband
    CHECK(p.select_lod(12.1, 0) == 1);   // crossed → demote to LOD1
}

TEST_CASE("select_lod: hysteresis prevents thrashing at LOD1/LOD2 boundary") {
    auto cfg = make_cfg();
    LodPredictor p(Scenario{}, cfg);

    // From LOD1 (max 30). leave_far = 36.
    CHECK(p.select_lod(35.0, 1) == 1);   // still LOD1
    CHECK(p.select_lod(37.0, 1) == 2);   // demote
    // From LOD2 (max 100). Re-enter LOD1 requires d ≤ 30 (not 36 — no
    // hysteresis on getting closer).
    CHECK(p.select_lod(31.0, 2) == 2);   // still LOD2
    CHECK(p.select_lod(30.0, 2) == 1);   // upgrade
}

TEST_CASE("LodPredictor issues prefetches as NPC crosses bands") {
    auto cfg = make_cfg();
    Scenario s = one_npc(5,
        {{0, 80.0}, {500, 5.0}},   // far → very close over 500 ms
        1000);

    Clock clk; EventQueue q; RecordingSink sink;
    AIWeightInjector inj(cfg.ai_weights);
    LodPredictor pred(s, cfg);
    pred.start(clk, q, inj, sink);
    while (q.pop_and_run(clk)) {}

    // Should see at least one LOD0 prefetch by end of run.
    bool saw_lod0 = false;
    for (const auto& t : sink.txns) {
        if (t.size_bytes == 100u * 1024u * 1024u) saw_lod0 = true;
    }
    CHECK(saw_lod0);
}

TEST_CASE("LodPredictor in-flight tracking suppresses duplicate prefetches") {
    auto cfg = make_cfg();
    // NPC stays at fixed distance 5m → cold start picks LOD2 (conservative),
    // then immediately upgrades to LOD0 on next tick. After issuing each
    // prefetch we should NOT re-issue the same (npc, lod) pair.
    Scenario s = one_npc(7, {{0, 5.0}, {1000, 5.0}}, 1000);

    Clock clk; EventQueue q; RecordingSink sink;
    AIWeightInjector inj(cfg.ai_weights);
    LodPredictor pred(s, cfg);
    pred.start(clk, q, inj, sink);
    while (q.pop_and_run(clk)) {}

    // Count prefetches per (size). With in-flight + resident tracking, each
    // unique (npc, lod) appears at most once before completion notification.
    // Since the scheduler isn't running in this test, completions never fire,
    // so resident_ stays empty; in_flight_ must prevent duplicates.
    int lod0 = 0, lod1 = 0, lod2 = 0;
    for (const auto& t : sink.txns) {
        if      (t.size_bytes == 100u * 1024u * 1024u) lod0++;
        else if (t.size_bytes ==  30u * 1024u * 1024u) lod1++;
        else if (t.size_bytes ==  10u * 1024u * 1024u) lod2++;
    }
    CHECK(lod0 <= 1);
    CHECK(lod1 <= 1);
    CHECK(lod2 <= 1);
}

TEST_CASE("LodPredictor: NPC stays beyond all bands → no prefetch") {
    auto cfg = make_cfg();
    Scenario s = one_npc(9, {{0, 200.0}, {1000, 150.0}}, 1000);

    Clock clk; EventQueue q; RecordingSink sink;
    AIWeightInjector inj(cfg.ai_weights);
    LodPredictor pred(s, cfg);
    pred.start(clk, q, inj, sink);
    while (q.pop_and_run(clk)) {}

    CHECK(sink.txns.empty());
}

TEST_CASE("LodPredictor: on_evict allows re-fetch of evicted LOD") {
    auto cfg = make_cfg();
    LodPredictor p(Scenario{}, cfg);
    p.on_complete(5, 0);
    CHECK(p.is_resident(5, 0));
    p.on_evict(5, 0);
    CHECK_FALSE(p.is_resident(5, 0));
}

TEST_CASE("make_predictor: unknown policy throws") {
    auto cfg = make_cfg();
    CHECK_THROWS_AS(make_predictor("bogus", Scenario{}, cfg), std::invalid_argument);
}

TEST_CASE("make_predictor: scripted and lod both constructable") {
    auto cfg = make_cfg();
    auto scripted = make_predictor("scripted", Scenario{}, cfg);
    auto lod      = make_predictor("lod", Scenario{}, cfg);
    CHECK(scripted != nullptr);
    CHECK(lod != nullptr);
}
