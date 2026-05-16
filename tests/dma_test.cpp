#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "neurostream/clock.hpp"
#include "neurostream/event_queue.hpp"
#include "neurostream/scheduler.hpp"

using namespace neurostream;

static Config make_cfg(const std::string& dma_path) {
    Config c;
    c.ssd = {5500, 8500, 2.0, 5};
    c.bus = {16000, 1};
    c.npu = {4, 512};
    c.audio = {5, 256, 1000, "critical"};
    c.texture = {500, 100, 262144, "normal"};
    c.ai_weights = {100, 30, 10, "high"};
    c.scheduler = {"qos", 100, 5, {2, 1}};
    c.dma.path = dma_path;
    c.dma.bounce.memcpy_bandwidth_mbps = 12000;
    c.dma.bounce.cycles_per_byte = 3;
    c.dma.neuro_dma.sgl_entry_bytes = 1048576;
    c.dma.neuro_dma.setup_cost_cycles = 1000;
    c.eviction = {"distance_lru"};
    c.degradation = {50};
    return c;
}

static Transaction weight(std::uint64_t id, std::uint32_t bytes) {
    Transaction t;
    t.id = id; t.source = SourceKind::Weight; t.qos = QosTag::High;
    t.size_bytes = bytes; t.issued_at = 0;
    return t;
}

TEST_CASE("neuro_dma path: 100 MB weight at full bus bandwidth (~6,553 µs)") {
    Config c = make_cfg("neuro_dma");
    Clock clk; EventQueue q;
    auto s = make_scheduler(c, clk, q, nullptr);

    auto tx = weight(1, 100u * 1024u * 1024u);
    s->accept(tx);
    while (q.pop_and_run(clk)) {}

    // 100 MB / 16 GB/s ≈ 6,553 µs.  Allow ±1 quantum tolerance.
    CHECK(s->kpi().completed == 1);
    CHECK(clk.now() >= 6500);
    CHECK(clk.now() <= 6700);
}

TEST_CASE("bounce path: 100 MB weight takes ~3.3× longer (3-segment formula)") {
    Config c = make_cfg("bounce");
    Clock clk; EventQueue q;
    auto s = make_scheduler(c, clk, q, nullptr);

    auto tx = weight(1, 100u * 1024u * 1024u);
    s->accept(tx);
    while (q.pop_and_run(clk)) {}

    // bus=16000, memcpy=12000 → effective = 16000*12000/(2*12000+16000) = 4800.
    // 100 MB / 4800 ≈ 21,845 µs.  Allow some quantum tolerance.
    CHECK(s->kpi().completed == 1);
    CHECK(clk.now() >= 21500);
    CHECK(clk.now() <= 22500);
}

TEST_CASE("bounce path burns massive CPU cycles; neuro_dma path burns ~1k") {
    auto run_one = [](const std::string& path) -> std::uint64_t {
        Config c = make_cfg(path);
        Clock clk; EventQueue q;
        auto s = make_scheduler(c, clk, q, nullptr);
        s->accept(weight(1, 100u * 1024u * 1024u));
        while (q.pop_and_run(clk)) {}
        return s->kpi().cpu_cycles_used;
    };

    auto bounce_cycles = run_one("bounce");
    auto npu_cycles    = run_one("neuro_dma");

    // bounce ≈ 100 MB × 3 cycles/byte = ~314 M cycles
    CHECK(bounce_cycles >= 300'000'000ull);
    CHECK(bounce_cycles <= 320'000'000ull);

    // neuro_dma = setup_cost_cycles (= 1000)
    CHECK(npu_cycles == 1000);

    // The headline ratio: bounce is at least 100,000× more CPU work.
    CHECK(bounce_cycles / npu_cycles > 100'000ull);
}

TEST_CASE("neuro_dma SGL entry count = size / sgl_entry_bytes") {
    Config c = make_cfg("neuro_dma");
    Clock clk; EventQueue q;
    auto s = make_scheduler(c, clk, q, nullptr);

    s->accept(weight(1, 100u * 1024u * 1024u));   // 100 MB
    while (q.pop_and_run(clk)) {}

    // 100 MB / 1 MB = 100 entries
    CHECK(s->kpi().sgl_entries_total == 100);
}

TEST_CASE("bounce SGL entry count = 1 (no SGL)") {
    Config c = make_cfg("bounce");
    Clock clk; EventQueue q;
    auto s = make_scheduler(c, clk, q, nullptr);

    s->accept(weight(1, 100u * 1024u * 1024u));
    while (q.pop_and_run(clk)) {}

    CHECK(s->kpi().sgl_entries_total == 1);
}

TEST_CASE("audio and texture are unaffected by dma.path") {
    auto run_one = [](const std::string& path) -> Time {
        Config c = make_cfg(path);
        Clock clk; EventQueue q;
        auto s = make_scheduler(c, clk, q, nullptr);
        Transaction tex;
        tex.id = 1; tex.source = SourceKind::Texture; tex.qos = QosTag::Normal;
        tex.size_bytes = 1'600'000; tex.issued_at = 0;
        s->accept(tex);
        while (q.pop_and_run(clk)) {}
        return clk.now();
    };
    CHECK(run_one("bounce")    == run_one("neuro_dma"));
}
