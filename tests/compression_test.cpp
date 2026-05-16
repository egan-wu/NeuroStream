#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "neurostream/clock.hpp"
#include "neurostream/event_queue.hpp"
#include "neurostream/scheduler.hpp"

using namespace neurostream;

static Config make_cfg(const std::string& dma_path,
                       const std::string& compression_path) {
    Config c;
    c.ssd = {5500, 8500, 2.0, 5};
    c.bus = {16000, 1};
    c.npu = {4, 512};
    c.audio = {5, 256, 1000, "critical"};
    c.texture = {500, 100, 262144, "normal"};
    c.ai_weights = {100, 30, 10, "high"};
    c.scheduler = {"qos", 100, 5, {2, 1}};
    c.dma.path = dma_path;
    c.dma.bounce = {12000, 3};
    c.dma.neuro_dma = {1048576, 1000};
    c.compression.path = compression_path;
    c.compression.weight_ratio = 2.0;
    c.compression.texture_ratio = 2.0;
    c.compression.cpu = {5, 1500};
    c.compression.inline_hw = {16000, 1000};
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

TEST_CASE("inline_hw boosts neuro_dma effective bandwidth × ratio (cap at decompressor)") {
    auto cfg = make_cfg("neuro_dma", "inline_hw");
    Clock clk; EventQueue q;
    auto s = make_scheduler(cfg, clk, q, nullptr);

    // 100 MB weight.
    // neuro_dma alone: 100 MB / 16 GB/s ≈ 6,553 µs
    // + inline_hw (ratio 2.0, decompressor 16 GB/s):
    //   boosted = 16 * 2 = 32 GB/s but capped at 16 GB/s decompressor
    //   → effective = 16 GB/s, same as plain neuro_dma (decompressor-bound)
    s->accept(weight(1, 100u * 1024u * 1024u));
    while (q.pop_and_run(clk)) {}

    CHECK(s->kpi().completed == 1);
    CHECK(clk.now() >= 6500);
    CHECK(clk.now() <= 6700);
}

TEST_CASE("inline_hw with high decompressor_bw doubles effective bandwidth") {
    auto cfg = make_cfg("neuro_dma", "inline_hw");
    // Bump decompressor_bw beyond bus×ratio so the cap doesn't bind.
    cfg.compression.inline_hw.decompressor_bw_mbps = 64000;
    Clock clk; EventQueue q;
    auto s = make_scheduler(cfg, clk, q, nullptr);

    s->accept(weight(1, 100u * 1024u * 1024u));
    while (q.pop_and_run(clk)) {}

    // boosted = 16 × 2 = 32 GB/s, cap = 64 GB/s → eff = 32 GB/s
    // 100 MB / 32 GB/s ≈ 3,276 µs
    CHECK(clk.now() >= 3200);
    CHECK(clk.now() <= 3400);
}

TEST_CASE("cpu compression caps effective bw at decompress throughput") {
    auto cfg = make_cfg("neuro_dma", "cpu");
    Clock clk; EventQueue q;
    auto s = make_scheduler(cfg, clk, q, nullptr);

    // neuro_dma + cpu: boosted = 16*2 = 32 GB/s, but capped at 1.5 GB/s.
    // 100 MB / 1.5 GB/s ≈ 70,000 µs
    s->accept(weight(1, 100u * 1024u * 1024u));
    while (q.pop_and_run(clk)) {}

    CHECK(clk.now() >= 65000);
    CHECK(clk.now() <= 75000);
}

TEST_CASE("cpu compression accumulates decompress_cycles_used") {
    auto cfg = make_cfg("neuro_dma", "cpu");
    Clock clk; EventQueue q;
    auto s = make_scheduler(cfg, clk, q, nullptr);

    s->accept(weight(1, 100u * 1024u * 1024u));   // 100 × 1024 × 1024 bytes
    while (q.pop_and_run(clk)) {}

    // expected: 100 MB × 5 cycles/byte
    auto expected = static_cast<std::uint64_t>(100ull * 1024 * 1024) * 5ull;
    CHECK(s->kpi().decompress_cycles_used == expected);
    // cpu_cycles is just the neuro_dma setup_cost
    CHECK(s->kpi().cpu_cycles_used == 1000);
}

TEST_CASE("inline_hw accumulates ONLY setup cycles, no decompress cycles") {
    auto cfg = make_cfg("neuro_dma", "inline_hw");
    Clock clk; EventQueue q;
    auto s = make_scheduler(cfg, clk, q, nullptr);

    s->accept(weight(1, 100u * 1024u * 1024u));
    while (q.pop_and_run(clk)) {}

    // 1000 (neuro_dma setup) + 1000 (inline_hw setup) = 2000
    CHECK(s->kpi().cpu_cycles_used == 2000);
    CHECK(s->kpi().decompress_cycles_used == 0);
}

TEST_CASE("none path: no compression effect") {
    auto cfg = make_cfg("neuro_dma", "none");
    Clock clk; EventQueue q;
    auto s = make_scheduler(cfg, clk, q, nullptr);

    s->accept(weight(1, 100u * 1024u * 1024u));
    while (q.pop_and_run(clk)) {}

    // Plain neuro_dma: ~6553 µs
    CHECK(clk.now() >= 6500);
    CHECK(clk.now() <= 6700);
    CHECK(s->kpi().decompress_cycles_used == 0);
    CHECK(s->kpi().cpu_cycles_used == 1000);   // neuro_dma setup only
}

TEST_CASE("texture also benefits from inline_hw compression") {
    auto cfg = make_cfg("neuro_dma", "inline_hw");
    // Texture transaction (256 KB) — outside the dma branch, but compression
    // should still apply. Effective bw → min(bus×ratio, decompressor_bw)
    Clock clk; EventQueue q;
    auto s = make_scheduler(cfg, clk, q, nullptr);

    Transaction tex;
    tex.id = 1; tex.source = SourceKind::Texture; tex.qos = QosTag::Normal;
    tex.size_bytes = 1'000'000; tex.issued_at = 0;
    s->accept(tex);
    while (q.pop_and_run(clk)) {}

    // 1 MB at min(16*2=32, 16)=16 GB/s = ~62 µs.
    CHECK(s->kpi().completed == 1);
    CHECK(clk.now() <= 100);
}

TEST_CASE("audio is unaffected by compression path") {
    auto cfg_none = make_cfg("neuro_dma", "none");
    auto cfg_hw   = make_cfg("neuro_dma", "inline_hw");

    auto run = [](const Config& c) {
        Clock clk; EventQueue q;
        auto s = make_scheduler(c, clk, q, nullptr);
        Transaction a;
        a.id = 1; a.source = SourceKind::Audio; a.qos = QosTag::Critical;
        a.size_bytes = 256; a.deadline = 1000; a.issued_at = 0;
        s->accept(a);
        while (q.pop_and_run(clk)) {}
        return clk.now();
    };
    // Same time under both compression modes.
    CHECK(run(cfg_none) == run(cfg_hw));
}
