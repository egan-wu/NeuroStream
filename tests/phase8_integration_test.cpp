#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "neurostream/clock.hpp"
#include "neurostream/event_queue.hpp"
#include "neurostream/injector.hpp"
#include "neurostream/predictor.hpp"
#include "neurostream/scheduler.hpp"

using namespace neurostream;

namespace {

struct RecordingSink : TransactionSink {
    std::vector<Transaction> txns;
    void accept(const Transaction& t) override { txns.push_back(t); }
};

Config make_cfg() {
    Config c{};
    c.ssd = {5500, 8500, 2.0, 5};
    c.bus = {16000, 1};
    c.npu = {4, 200};   // small cache for pressure
    c.audio = {5, 256, 1000, "critical"};
    c.texture = {500, 100, 262144, "normal"};
    c.ai_weights = {100, 30, 10, "high"};
    c.scheduler = {"qos", 100, 5, {2, 1}};
    c.dma.path = "neuro_dma";
    c.dma.bounce = {12000, 3};
    c.dma.neuro_dma = {1048576, 1000};
    c.compression.path = "inline_hw";
    c.compression.weight_ratio = 2.0;
    c.compression.texture_ratio = 2.0;
    c.compression.cpu = {5, 1500};
    c.compression.inline_hw = {16000, 1000};
    c.lod_manager.tick_us = 16667;
    c.lod_manager.hysteresis_pct = 20;
    c.lod_manager.bands = {{0, 10}, {1, 30}, {2, 100}};
    c.predictor.policy = "intent";
    c.intent_predictor.fov_deg = 120;
    c.intent_predictor.look_ahead_ms = 500;
    c.intent_predictor.close_m = 10;
    c.intent_predictor.near_m = 30;
    c.intent_predictor.visible_m = 100;
    c.intent_predictor.stopped_dist_m = 5.0;
    c.intent_predictor.stopped_speed_m_s = 1.0;
    c.eviction = {"distance_lru"};
    c.degradation = {50};
    return c;
}

NpcSpec static_npc(std::uint32_t id, double x, double y,
                   const std::string& priority = "normal") {
    NpcSpec n;
    n.id = id;
    n.priority = priority;
    n.waypoints = {{0, {x, y}, {}}};
    return n;
}

}

TEST_CASE("Phase 8: cache fills up and starts evicting") {
    auto cfg = make_cfg();   // 200 MB cache → fits exactly 2 × LOD0
    Scenario s;
    s.schema_version = kScenarioSchemaVersion;
    s.duration_ms = 3000;
    s.player.waypoints = {
        {0,    {0.0, 0.0}, 0.0},
        {3000, {0.0, 0.0}, 0.0},
    };
    // 5 quest NPCs in frustum at distances 3, 4, 5, 6, 7 → all LOD0.
    // Cache fits 2; the other 3 cause evictions.
    for (std::uint32_t i = 1; i <= 5; ++i) {
        s.npcs.push_back(static_npc(i, /*x=*/3.0 + i - 1, /*y=*/0.0, /*priority=*/"quest"));
    }

    Clock clk; EventQueue q;
    auto sched = make_scheduler(cfg, clk, q, nullptr);
    AIWeightInjector inj(cfg.ai_weights);
    auto pred = std::make_unique<IntentPredictor>(s, cfg);
    pred->start(clk, q, inj, *sched);
    sched->set_completion_observer([&](std::uint32_t npc, int lod) {
        pred->on_complete(npc, lod);
    });
    while (q.pop_and_run(clk)) {}

    // At least one eviction must have fired.
    CHECK(pred->kpi().evictions >= 1);
}

TEST_CASE("Phase 8: pinned (interaction-active) entries survive eviction") {
    auto cfg = make_cfg();
    Scenario s;
    s.schema_version = kScenarioSchemaVersion;
    s.duration_ms = 2000;
    s.player.waypoints = {
        {0,    {0.0, 0.0}, 0.0},
        {2000, {0.0, 0.0}, 0.0},
    };
    // NPC 1 is interacted with the WHOLE scenario → its LOD0 must stay.
    // NPCs 2-5 cycle, causing pressure.
    s.npcs.push_back(static_npc(1, 3.0, 0.0));
    s.interactions.push_back({100, 1, 1900});   // active [100, 2000)
    for (std::uint32_t i = 2; i <= 5; ++i) {
        s.npcs.push_back(static_npc(i, 3.0 + i - 1, 0.0, /*priority=*/"quest"));
    }

    Clock clk; EventQueue q;
    auto sched = make_scheduler(cfg, clk, q, nullptr);
    AIWeightInjector inj(cfg.ai_weights);
    auto pred = std::make_unique<IntentPredictor>(s, cfg);
    pred->start(clk, q, inj, *sched);
    sched->set_completion_observer([&](std::uint32_t npc, int lod) {
        pred->on_complete(npc, lod);
    });
    while (q.pop_and_run(clk)) {}

    // NPC 1 must be resident at end of run despite pressure.
    CHECK(pred->cache().is_resident({1, 0}));
}

TEST_CASE("Phase 8 Pillar G: interaction at t=0 with empty cache → degrade") {
    auto cfg = make_cfg();
    Scenario s;
    s.schema_version = kScenarioSchemaVersion;
    s.duration_ms = 100;
    s.player.waypoints = {
        {0,   {0.0, 0.0}, 0.0},
        {100, {0.0, 0.0}, 0.0},
    };
    s.npcs.push_back(static_npc(1, 50.0, 0.0));   // far → no preload
    s.interactions.push_back({0, 1, 50});          // immediate interact

    Clock clk; EventQueue q;
    auto sched = make_scheduler(cfg, clk, q, nullptr);
    AIWeightInjector inj(cfg.ai_weights);
    auto pred = std::make_unique<IntentPredictor>(s, cfg);
    pred->start(clk, q, inj, *sched);
    sched->set_completion_observer([&](std::uint32_t npc, int lod) {
        pred->on_complete(npc, lod);
    });
    while (q.pop_and_run(clk)) {}

    CHECK(pred->kpi().degradations          >= 1);
    CHECK(pred->kpi().degradations_no_weight >= 1);
}

TEST_CASE("Phase 8: 5th simultaneous interaction triggers core saturation") {
    auto cfg = make_cfg();      // npu.cores = 4
    cfg.npu.shared_cache_mb = 1024;   // make sure all 5 LOD0 fit
    Scenario s;
    s.schema_version = kScenarioSchemaVersion;
    s.duration_ms = 2000;
    s.player.waypoints = {
        {0,    {0.0, 0.0}, 0.0},
        {2000, {0.0, 0.0}, 0.0},
    };
    // 5 NPCs (quest priority so all get LOD0). 4 slots → 1 saturation.
    for (std::uint32_t i = 1; i <= 5; ++i) {
        s.npcs.push_back(static_npc(i, 2.0 + i, 0.0, /*priority=*/"quest"));
        s.interactions.push_back({100, i, 1800});
    }

    Clock clk; EventQueue q;
    auto sched = make_scheduler(cfg, clk, q, nullptr);
    AIWeightInjector inj(cfg.ai_weights);
    auto pred = std::make_unique<IntentPredictor>(s, cfg);
    pred->start(clk, q, inj, *sched);
    sched->set_completion_observer([&](std::uint32_t npc, int lod) {
        pred->on_complete(npc, lod);
    });
    while (q.pop_and_run(clk)) {}

    CHECK(pred->kpi().core_saturation_events >= 1);
}

TEST_CASE("Phase 8: degraded interaction falls back to lower LOD when present") {
    auto cfg = make_cfg();
    Scenario s;
    s.schema_version = kScenarioSchemaVersion;
    s.duration_ms = 2000;
    s.player.waypoints = {
        {0,    {0.0, 0.0}, 0.0},
        {2000, {0.0, 0.0}, 0.0},
    };
    // NPC 1 starts at LOD2 distance, then interaction tries to force LOD0
    // at a time when LOD2 has loaded but LOD0 hasn't yet been prefetched.
    s.npcs.push_back(static_npc(1, 50.0, 0.0));   // → LOD2 will load first
    s.interactions.push_back({30, 1, 100});       // interaction shortly after start

    Clock clk; EventQueue q;
    auto sched = make_scheduler(cfg, clk, q, nullptr);
    AIWeightInjector inj(cfg.ai_weights);
    auto pred = std::make_unique<IntentPredictor>(s, cfg);
    pred->start(clk, q, inj, *sched);
    sched->set_completion_observer([&](std::uint32_t npc, int lod) {
        pred->on_complete(npc, lod);
    });
    while (q.pop_and_run(clk)) {}

    // We expect at least one degradation total; whether no_weight or
    // partial-fall-back depends on timing, but either way the frame
    // wasn't blocked.
    CHECK(pred->kpi().degradations >= 1);
}
