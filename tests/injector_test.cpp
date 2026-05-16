#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "neurostream/clock.hpp"
#include "neurostream/event_queue.hpp"
#include "neurostream/injector.hpp"
#include "neurostream/predictor.hpp"
#include "neurostream/scenario.hpp"
#include "neurostream/transaction.hpp"
#include <vector>

using namespace neurostream;

namespace {

class RecordingSink : public TransactionSink {
public:
    void accept(const Transaction& t) override { txns.push_back(t); }
    std::vector<Transaction> txns;
};

AudioConfig audio_cfg() {
    AudioConfig c;
    c.rate_mbps    = 5;
    c.packet_bytes = 256;
    c.deadline_us  = 1000;
    c.priority     = "critical";
    return c;
}

TextureConfig texture_cfg() {
    TextureConfig c;
    c.burst_rate_mbps   = 500;
    c.burst_duration_ms = 100;
    c.block_bytes       = 262144;
    c.priority          = "normal";
    return c;
}

AiWeightsConfig weights_cfg() {
    AiWeightsConfig c;
    c.lod0_mb  = 100;
    c.lod1_mb  = 30;
    c.lod2_mb  = 10;
    c.priority = "high";
    return c;
}

}

TEST_CASE("audio injector emits at the configured period and stops at duration") {
    Clock clk;
    EventQueue q;
    RecordingSink sink;

    AudioTrafficGen gen(audio_cfg());
    gen.start(clk, q, sink, /*stop_at_us=*/1000);   // 1 ms run

    while (q.pop_and_run(clk)) {}

    // period = 256 / 5 = 51 us → ~19 packets in first 1 ms
    CHECK(sink.txns.size() >= 18);
    CHECK(sink.txns.size() <= 20);
    for (const auto& t : sink.txns) {
        CHECK(t.source == SourceKind::Audio);
        CHECK(t.qos == QosTag::Critical);
        CHECK(t.size_bytes == 256);
        CHECK(t.deadline > t.issued_at);
    }
    // Issued times strictly increasing.
    for (std::size_t i = 1; i < sink.txns.size(); ++i) {
        CHECK(sink.txns[i].issued_at > sink.txns[i-1].issued_at);
    }
}

TEST_CASE("texture burst emits expected count and tags") {
    Clock clk;
    EventQueue q;
    RecordingSink sink;

    TextureTrafficGen gen(texture_cfg());
    // 50 ms × 500 MB/s = 25 MB; with 256 KB blocks → ~95 blocks
    gen.schedule_burst(clk, q, sink, /*start*/ 0, /*rate*/ 500, /*ms*/ 50);

    while (q.pop_and_run(clk)) {}
    CHECK(sink.txns.size() >= 90);
    CHECK(sink.txns.size() <= 100);
    for (const auto& t : sink.txns) {
        CHECK(t.source == SourceKind::Texture);
        CHECK(t.qos == QosTag::Normal);
        CHECK(t.size_bytes == 262144);
        CHECK(t.deadline == 0);
    }
}

TEST_CASE("weight injector schedules at requested time with correct LOD size") {
    Clock clk;
    EventQueue q;
    RecordingSink sink;

    AIWeightInjector inj(weights_cfg());
    inj.schedule_prefetch(clk, q, sink, /*at_us*/ 1000, /*npc*/ 42, /*lod*/ 1);
    inj.schedule_prefetch(clk, q, sink, /*at_us*/ 2000, /*npc*/ 99, /*lod*/ 0);

    while (q.pop_and_run(clk)) {}
    REQUIRE(sink.txns.size() == 2);
    CHECK(sink.txns[0].issued_at == 1000);
    CHECK(sink.txns[0].source == SourceKind::Weight);
    CHECK(sink.txns[0].source_inst == 42);
    CHECK(sink.txns[0].qos == QosTag::High);
    CHECK(sink.txns[0].size_bytes == 30u * 1024 * 1024);

    CHECK(sink.txns[1].issued_at == 2000);
    CHECK(sink.txns[1].source_inst == 99);
    CHECK(sink.txns[1].size_bytes == 100u * 1024 * 1024);
}

TEST_CASE("invalid LOD throws") {
    Clock clk; EventQueue q; RecordingSink sink;
    AIWeightInjector inj(weights_cfg());
    CHECK_THROWS_AS(inj.schedule_prefetch(clk, q, sink, 0, 1, 9), std::invalid_argument);
}

TEST_CASE("ScriptedPredictor loads LOD0 for every NPC at t=0") {
    Clock clk; EventQueue q; RecordingSink sink;
    AIWeightInjector inj(weights_cfg());

    Scenario s;
    s.npcs = {
        NpcSpec{1, {{0, {50.0, 0.0}, {}}}, "normal"},
        NpcSpec{2, {{0, {20.0, 0.0}, {}}, {1000, {80.0, 0.0}, {}}}, "normal"},
        NpcSpec{3, {{0, { 5.0, 0.0}, {}}}, "normal"},
    };
    ScriptedPredictor p(s);
    p.start(clk, q, inj, sink);

    while (q.pop_and_run(clk)) {}
    REQUIRE(sink.txns.size() == 3);
    for (const auto& tx : sink.txns) {
        CHECK(tx.issued_at == 0);
        CHECK(tx.size_bytes == 100u * 1024u * 1024u);   // LOD0 always
    }
}
