#pragma once
#include "neurostream/trace.hpp"
#include "neurostream/transaction.hpp"

namespace neurostream {

// Phase 2 placeholder sink. Records every transaction as an Issue event in
// the trace. Phase 3 replaces this with a real scheduler that will then own
// emitting Issue/Arrive/Complete records itself.
class TraceSink : public TransactionSink {
public:
    explicit TraceSink(TraceWriter& tw) : tw_(tw) {}

    void accept(const Transaction& t) override {
        TraceRecord r{};
        r.timestamp_us   = t.issued_at;
        r.source_id      = (static_cast<std::uint32_t>(t.source) << 24) | (t.source_inst & 0x00FFFFFF);
        r.event_type     = EventType::Issue;
        r.qos_tag        = t.qos;
        r.size_bytes     = t.size_bytes;
        r.latency_us     = 0;
        r.transaction_id = t.id;
        tw_.write(r);
        ++count_;
    }

    std::uint64_t count() const noexcept { return count_; }

private:
    TraceWriter&  tw_;
    std::uint64_t count_ = 0;
};

}
