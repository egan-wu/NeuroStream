#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "neurostream/trace.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace neurostream;
namespace fs = std::filesystem;

static fs::path tmp_path(const char* stem, const char* ext) {
    auto dir = fs::temp_directory_path() / "neurostream_tests";
    fs::create_directories(dir);
    return dir / (std::string(stem) + ext);
}

TEST_CASE("trace writes header and records to binary, mirrors to CSV") {
    auto bin = tmp_path("t1", ".bin");
    auto csv = tmp_path("t1", ".csv");

    {
        TraceWriter w(bin, csv);
        auto txn = w.next_transaction_id();
        TraceRecord r{};
        r.timestamp_us   = 12345;
        r.source_id      = 7;
        r.event_type     = EventType::Issue;
        r.qos_tag        = QosTag::Critical;
        r.size_bytes     = 4096;
        r.latency_us     = 0;
        r.transaction_id = txn;
        w.write(r);

        r.timestamp_us = 12500;
        r.event_type   = EventType::Complete;
        r.latency_us   = 155;
        w.write(r);
    }

    std::ifstream f(bin, std::ios::binary);
    REQUIRE(f.is_open());

    TraceHeader h{};
    f.read(reinterpret_cast<char*>(&h), sizeof(h));
    CHECK(std::memcmp(h.magic, "NSTR", 4) == 0);
    CHECK(h.version == kTraceVersion);
    CHECK(h.record_size == sizeof(TraceRecord));

    TraceRecord r1{}, r2{};
    f.read(reinterpret_cast<char*>(&r1), sizeof(r1));
    f.read(reinterpret_cast<char*>(&r2), sizeof(r2));
    CHECK(r1.timestamp_us == 12345);
    CHECK(r1.event_type == EventType::Issue);
    CHECK(r1.transaction_id == 1);
    CHECK(r2.timestamp_us == 12500);
    CHECK(r2.event_type == EventType::Complete);
    CHECK(r2.latency_us == 155);

    CHECK(fs::file_size(csv) > 0);
}

TEST_CASE("transaction ids are unique and monotonic") {
    auto bin = tmp_path("t2", ".bin");
    auto csv = tmp_path("t2", ".csv");
    TraceWriter w(bin, csv);
    auto a = w.next_transaction_id();
    auto b = w.next_transaction_id();
    auto c = w.next_transaction_id();
    CHECK(b == a + 1);
    CHECK(c == b + 1);
}

TEST_CASE("empty path skips that output") {
    auto csv = tmp_path("t3", ".csv");
    {
        TraceWriter w("", csv);
        TraceRecord r{};
        r.timestamp_us = 1;
        w.write(r);
    }
    CHECK(fs::file_size(csv) > 0);
}
