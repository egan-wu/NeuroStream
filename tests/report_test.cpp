#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "neurostream/latency_histogram.hpp"
#include "neurostream/report_writer.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace neurostream;

// ----- LatencyHistogram -----

TEST_CASE("histogram: empty has count 0 and percentiles 0") {
    LatencyHistogram h;
    CHECK(h.count() == 0);
    CHECK(h.percentile(0.99) == 0);
    CHECK(h.mean_us() == doctest::Approx(0.0));
}

TEST_CASE("histogram: records and reports max / mean / count correctly") {
    LatencyHistogram h;
    h.record(10); h.record(20); h.record(30);
    CHECK(h.count() == 3);
    CHECK(h.max() == 30);
    CHECK(h.mean_us() == doctest::Approx(20.0));
}

TEST_CASE("histogram: P99 is bucket-rounded but ordered correctly") {
    LatencyHistogram h;
    for (int i = 0; i < 99; ++i) h.record(10);
    h.record(1000);   // one outlier
    // P99 of [10×99, 1000] is the bucket containing 1000.
    auto p99 = h.percentile(0.99);
    CHECK(p99 >= 10);
    CHECK(p99 <= 2048);   // bucket containing 1000 µs
}

TEST_CASE("histogram: merge sums counts and tracks max") {
    LatencyHistogram a, b;
    a.record(50); a.record(60);
    b.record(70); b.record(800);
    a.merge(b);
    CHECK(a.count() == 4);
    CHECK(a.max() == 800);
}

// ----- ReportWriter formatting -----

namespace {
LadderRow mk_row(const std::string& label,
                 Time p99_seed, Time wmax,
                 std::uint64_t cpu, std::uint64_t decomp,
                 std::uint64_t evict, std::uint64_t degrade) {
    LadderRow r;
    r.label = label;
    r.sched_kpi.audio_lat_hist.record(p99_seed);
    r.sched_kpi.audio_lat_max = p99_seed;
    r.sched_kpi.audio_lat_mean = static_cast<double>(p99_seed);
    r.sched_kpi.weight_lat_max = wmax;
    r.sched_kpi.cpu_cycles_used = cpu;
    r.sched_kpi.decompress_cycles_used = decomp;
    r.pred_kpi.evictions = evict;
    r.pred_kpi.degradations = degrade;
    return r;
}
}

TEST_CASE("ReportWriter::format_markdown contains scenario header and baseline row") {
    std::vector<LadderRow> rows = {
        mk_row("fifo+bounce+none", 1000, 40000, 1'000'000'000, 0, 0, 0),
        mk_row("qos+neuro_dma+inline_hw", 50, 10000, 100'000, 0, 0, 0),
    };
    auto md = ReportWriter::format_markdown("world", 5000, rows);
    CHECK(md.find("# Scenario: world") != std::string::npos);
    CHECK(md.find("5000 ms") != std::string::npos);
    CHECK(md.find("fifo+bounce+none") != std::string::npos);
    CHECK(md.find("qos+neuro_dma+inline_hw") != std::string::npos);
    // Baseline row has no improvement annotation; second row should.
    CHECK(md.find("×") != std::string::npos);
}

TEST_CASE("ReportWriter::format_csv has expected header and row count") {
    std::vector<LadderRow> rows = {
        mk_row("a", 100, 1000, 10000, 0, 5, 1),
        mk_row("b", 50,  500,  1000,  0, 2, 0),
    };
    auto csv = ReportWriter::format_csv(rows);
    std::stringstream ss(csv);
    std::string line;
    int count = 0;
    while (std::getline(ss, line)) ++count;
    CHECK(count == 3);   // header + 2 data rows
    CHECK(csv.find("config,") == 0);
    CHECK(csv.find(",evictions,") != std::string::npos);
}

TEST_CASE("ReportWriter: write_summary creates summary.md and summary.csv") {
    auto dir = std::filesystem::temp_directory_path() / "ns_report_test";
    std::filesystem::remove_all(dir);

    std::vector<LadderRow> rows = {
        mk_row("baseline", 1000, 40000, 1'000'000, 0, 0, 0),
        mk_row("full_stack", 1, 10000, 1000, 0, 100, 0),
    };
    ReportWriter::write_summary(dir, "test", 1000, rows);

    CHECK(std::filesystem::exists(dir / "summary.md"));
    CHECK(std::filesystem::exists(dir / "summary.csv"));

    std::ifstream f(dir / "summary.csv");
    std::string line;
    int count = 0;
    while (std::getline(f, line)) ++count;
    CHECK(count == 3);   // header + 2 data rows
}

TEST_CASE("ReportWriter: round-trip CSV preserves the integer KPIs") {
    auto dir = std::filesystem::temp_directory_path() / "ns_report_rt";
    std::filesystem::remove_all(dir);

    std::vector<LadderRow> rows = {
        mk_row("a", 50, 5000, 1500000, 750000, 3, 1),
    };
    ReportWriter::write_summary(dir, "rt", 100, rows);
    auto read = ReportWriter::read_csv(dir / "summary.csv");
    REQUIRE(read.size() == 1);
    CHECK(read[0].label == "a");
    CHECK(read[0].sched_kpi.cpu_cycles_used == 1500000);
    CHECK(read[0].sched_kpi.decompress_cycles_used == 750000);
    CHECK(read[0].pred_kpi.evictions == 3);
    CHECK(read[0].pred_kpi.degradations == 1);
}
