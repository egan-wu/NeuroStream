#pragma once
#include "neurostream/time.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>

namespace neurostream {

enum class EventType : std::uint8_t {
    Issue    = 0,
    Arrive   = 1,
    Complete = 2,
    Drop     = 3,
    Cancel   = 4,
};

enum class QosTag : std::uint8_t {
    Critical = 0,
    High     = 1,
    Normal   = 2,
};

#pragma pack(push, 1)
struct TraceHeader {
    char          magic[4];        // "NSTR"
    std::uint32_t version;         // 1
    std::uint32_t record_size;     // sizeof(TraceRecord) = 32
    std::uint32_t reserved;
};
static_assert(sizeof(TraceHeader) == 16);

struct TraceRecord {
    std::int64_t  timestamp_us;
    std::uint32_t source_id;
    EventType     event_type;
    QosTag        qos_tag;
    std::uint16_t reserved;
    std::uint32_t size_bytes;
    std::uint32_t latency_us;
    std::uint64_t transaction_id;
};
static_assert(sizeof(TraceRecord) == 32);
#pragma pack(pop)

inline constexpr std::uint32_t kTraceVersion = 1;

class TraceWriter {
public:
    // Either path may be empty to skip that output. Streams are opened on
    // construction; records flush via the OS buffer and are guaranteed
    // durable on destruction.
    TraceWriter(const std::filesystem::path& binary_path,
                const std::filesystem::path& csv_path);
    ~TraceWriter();

    TraceWriter(const TraceWriter&)            = delete;
    TraceWriter& operator=(const TraceWriter&) = delete;

    void write(const TraceRecord& rec);

    std::uint64_t next_transaction_id() noexcept { return next_txn_++; }

private:
    std::ofstream bin_;
    std::ofstream csv_;
    std::uint64_t next_txn_ = 1;
};

std::string_view to_string(EventType e) noexcept;
std::string_view to_string(QosTag q) noexcept;

}
