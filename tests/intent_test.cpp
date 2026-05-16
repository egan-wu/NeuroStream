#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "neurostream/predictor.hpp"
#include "neurostream/transaction.hpp"

using namespace neurostream;

namespace {

struct RecSink : TransactionSink {
    std::vector<Transaction> txns;
    void accept(const Transaction& t) override { txns.push_back(t); }
};

Config make_cfg() {
    Config c{};
    c.ssd = {5500, 8500, 2.0, 5};
    c.bus = {16000, 1};
    c.npu = {4, 512};
    c.audio = {5, 256, 1000, "critical"};
    c.texture = {500, 100, 262144, "normal"};
    c.ai_weights = {100, 30, 10, "high"};
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
    c.scheduler = {"qos", 100, 5, {2, 1}};
    c.dma.path = "neuro_dma";
    c.dma.bounce = {12000, 3};
    c.dma.neuro_dma = {1048576, 1000};
    c.eviction = {"distance_lru"};
    c.degradation = {50};
    return c;
}

// Scenario template: stationary player at (0,0) facing +x for `dur_ms`.
Scenario base_scenario(int dur_ms = 1000) {
    Scenario s;
    s.schema_version = kScenarioSchemaVersion;
    s.duration_ms = dur_ms;
    s.player.waypoints = {
        {0,      {0.0, 0.0}, 0.0},
        {dur_ms, {0.0, 0.0}, 0.0},
    };
    return s;
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

// ----- cascade unit tests (direct select_lod calls) -----

TEST_CASE("cascade rule 1: quest NPC always wins LOD0") {
    auto cfg = make_cfg();
    IntentPredictor p(Scenario{}, cfg);
    IntentPredictor::NpcSnapshot s;
    NpcSpec q; q.priority = "quest";
    s.npc = &q;
    s.quest = true;
    s.distance_m = 999.0;     // does not matter
    s.in_frustum = false;
    CHECK(p.select_lod(s, /*stopped=*/false) == 0);
}

TEST_CASE("cascade rule 2: active interaction forces LOD0") {
    auto cfg = make_cfg();
    IntentPredictor p(Scenario{}, cfg);
    IntentPredictor::NpcSnapshot s;
    NpcSpec n;
    s.npc = &n;
    s.quest = false;
    s.interact_active = true;
    s.distance_m = 999.0;
    s.in_frustum = false;
    CHECK(p.select_lod(s, false) == 0);
}

TEST_CASE("cascade rule 3: stopped + in frustum + close → LOD0") {
    auto cfg = make_cfg();
    IntentPredictor p(Scenario{}, cfg);
    IntentPredictor::NpcSnapshot s;
    NpcSpec n;
    s.npc = &n;
    s.quest = false; s.interact_active = false;
    s.in_frustum = true;
    s.distance_m = 4.0;
    CHECK(p.select_lod(s, /*stopped=*/true) == 0);
}

TEST_CASE("cascade rule 4: in frustum + close (but moving) → LOD1") {
    auto cfg = make_cfg();
    IntentPredictor p(Scenario{}, cfg);
    IntentPredictor::NpcSnapshot s;
    NpcSpec n;
    s.npc = &n;
    s.quest = false; s.interact_active = false;
    s.in_frustum = true;
    s.distance_m = 8.0;     // < close_m
    CHECK(p.select_lod(s, /*stopped=*/false) == 1);
}

TEST_CASE("cascade rule 5: approaching at near distance → LOD1") {
    auto cfg = make_cfg();
    IntentPredictor p(Scenario{}, cfg);
    IntentPredictor::NpcSnapshot s;
    NpcSpec n;
    s.npc = &n;
    s.quest = false; s.interact_active = false;
    s.in_frustum = true;
    s.distance_m = 20.0;    // > close_m but < near_m
    s.approaching = true;
    CHECK(p.select_lod(s, false) == 1);
}

TEST_CASE("cascade rule 6: in frustum at near distance, not approaching → LOD2") {
    auto cfg = make_cfg();
    IntentPredictor p(Scenario{}, cfg);
    IntentPredictor::NpcSnapshot s;
    NpcSpec n;
    s.npc = &n;
    s.quest = false; s.interact_active = false;
    s.in_frustum = true;
    s.distance_m = 20.0;
    s.approaching = false;
    CHECK(p.select_lod(s, false) == 2);
}

TEST_CASE("cascade rule 7: outside frustum but visible → LOD2") {
    auto cfg = make_cfg();
    IntentPredictor p(Scenario{}, cfg);
    IntentPredictor::NpcSnapshot s;
    NpcSpec n;
    s.npc = &n;
    s.quest = false; s.interact_active = false;
    s.in_frustum = false;
    s.distance_m = 50.0;
    CHECK(p.select_lod(s, false) == 2);
}

TEST_CASE("cascade rule 8: beyond visible → no LOD") {
    auto cfg = make_cfg();
    IntentPredictor p(Scenario{}, cfg);
    IntentPredictor::NpcSnapshot s;
    NpcSpec n;
    s.npc = &n;
    s.quest = false; s.interact_active = false;
    s.in_frustum = false;
    s.distance_m = 200.0;
    CHECK(p.select_lod(s, false) == -1);
}

// ----- integration: town-traversal scenario -----

TEST_CASE("town traversal: 20 NPCs passed without facing → no LOD0") {
    auto cfg = make_cfg();
    Scenario s;
    s.schema_version = kScenarioSchemaVersion;
    s.duration_ms = 4000;
    // Player walks east 40m in 4s (10 m/s = above stopped threshold)
    s.player.waypoints = {
        {0,    {0.0,  0.0}, 0.0},
        {4000, {40.0, 0.0}, 0.0},
    };
    // 20 NPCs lining the street ±4m off-axis
    for (int i = 0; i < 20; ++i) {
        s.npcs.push_back(static_npc(static_cast<std::uint32_t>(i + 1),
                                    /*x=*/2.0 * (i + 1),
                                    /*y=*/(i % 2 == 0 ? 4.0 : -4.0)));
    }

    Clock clk; EventQueue q; RecSink sink;
    AIWeightInjector inj(cfg.ai_weights);
    IntentPredictor p(s, cfg);
    p.start(clk, q, inj, sink);
    while (q.pop_and_run(clk)) {}

    int lod0 = 0, lod2 = 0;
    for (const auto& t : sink.txns) {
        if      (t.size_bytes == 100u*1024u*1024u) lod0++;
        else if (t.size_bytes ==  10u*1024u*1024u) lod2++;
    }
    // Player is moving (not stopped), so rule 3 never fires.
    // NPCs are mostly perpendicular to player facing → outside or near
    // frustum boundary → bulk should be LOD2.
    CHECK(lod0 == 0);
    CHECK(lod2 > 0);
}

TEST_CASE("approaching NPC: velocity look-ahead → directly LOD0") {
    auto cfg = make_cfg();
    Scenario s;
    s.schema_version = kScenarioSchemaVersion;
    s.duration_ms = 1000;
    // Player stays put, facing +x
    s.player.waypoints = {
        {0,    {0.0, 0.0}, 0.0},
        {1000, {0.0, 0.0}, 0.0},
    };
    // NPC rushes from 80m to 5m in 500 ms → 150 m/s
    NpcSpec npc;
    npc.id = 1;
    npc.waypoints = { {0, {80.0, 0.0}, {}}, {500, {5.0, 0.0}, {}} };
    s.npcs.push_back(npc);

    Clock clk; EventQueue q; RecSink sink;
    AIWeightInjector inj(cfg.ai_weights);
    IntentPredictor p(s, cfg);
    p.start(clk, q, inj, sink);
    while (q.pop_and_run(clk)) {}

    // With look-ahead, at t=0 the future distance is 80 - 150*0.5 = 5m,
    // so IntentPredictor should fire LOD0 (stopped player + close future distance)
    // even though current distance is 80m.
    bool saw_lod0 = false;
    for (const auto& t : sink.txns) {
        if (t.size_bytes == 100u*1024u*1024u) saw_lod0 = true;
    }
    CHECK(saw_lod0);
}

TEST_CASE("quest NPC stays LOD0 even when far away") {
    auto cfg = make_cfg();
    Scenario s = base_scenario(1000);
    s.npcs.push_back(static_npc(1, /*x=*/50.0, /*y=*/0.0, /*priority=*/"quest"));

    Clock clk; EventQueue q; RecSink sink;
    AIWeightInjector inj(cfg.ai_weights);
    IntentPredictor p(s, cfg);
    p.start(clk, q, inj, sink);
    while (q.pop_and_run(clk)) {}

    bool saw_lod0 = false;
    for (const auto& t : sink.txns) {
        if (t.size_bytes == 100u*1024u*1024u) saw_lod0 = true;
    }
    CHECK(saw_lod0);
}

TEST_CASE("interaction event forces LOD0 during duration window") {
    auto cfg = make_cfg();
    Scenario s = base_scenario(1000);
    // NPC at 50m (would normally only get LOD2)
    s.npcs.push_back(static_npc(7, 50.0, 0.0));
    s.interactions.push_back({200, 7, 300});   // active in [200, 500] ms

    Clock clk; EventQueue q; RecSink sink;
    AIWeightInjector inj(cfg.ai_weights);
    IntentPredictor p(s, cfg);
    p.start(clk, q, inj, sink);
    while (q.pop_and_run(clk)) {}

    bool saw_lod0 = false;
    for (const auto& t : sink.txns) {
        if (t.source_inst == 7u && t.size_bytes == 100u*1024u*1024u) saw_lod0 = true;
    }
    CHECK(saw_lod0);
}

TEST_CASE("make_predictor: intent policy constructible") {
    auto cfg = make_cfg();
    auto p = make_predictor("intent", Scenario{}, cfg);
    CHECK(p != nullptr);
}
