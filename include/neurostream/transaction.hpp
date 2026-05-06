#pragma once
#include "neurostream/time.hpp"
#include "neurostream/trace.hpp"
#include <cstdint>

namespace neurostream {

enum class SourceKind : std::uint32_t {
    Audio   = 1,
    Texture = 2,
    Weight  = 3,
};

struct Transaction {
    std::uint64_t id            = 0;
    SourceKind    source        = SourceKind::Audio;
    std::uint32_t source_inst   = 0;     // per-source instance (npc id, etc.)
    QosTag        qos           = QosTag::Normal;
    std::uint32_t size_bytes    = 0;
    Time          issued_at     = 0;
    Time          deadline      = 0;     // 0 = no hard deadline
};

class TransactionSink {
public:
    virtual ~TransactionSink() = default;
    virtual void accept(const Transaction&) = 0;
};

}
