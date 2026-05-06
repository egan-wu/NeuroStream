#pragma once
#include "neurostream/clock.hpp"
#include "neurostream/config.hpp"
#include "neurostream/event_queue.hpp"
#include "neurostream/transaction.hpp"
#include <cstdint>

namespace neurostream {

QosTag parse_priority(const std::string& s);

class AudioTrafficGen {
public:
    AudioTrafficGen(const AudioConfig& cfg, std::uint32_t source_inst = 0);
    void start(Clock& clock, EventQueue& q, TransactionSink& sink, Time stop_at_us);

private:
    void schedule_next(Clock& clock, EventQueue& q, TransactionSink& sink, Time stop_at_us);

    AudioConfig    cfg_;
    std::uint32_t  source_inst_;
    Time           period_us_;
    QosTag         qos_;
    std::uint64_t  next_id_ = 0;
};

class TextureTrafficGen {
public:
    TextureTrafficGen(const TextureConfig& cfg, std::uint32_t source_inst = 0);

    // Trigger one burst of `rate_mbps` for `duration_ms` starting at `start_at_us`.
    void schedule_burst(Clock& clock, EventQueue& q, TransactionSink& sink,
                        Time start_at_us, int rate_mbps, int duration_ms);

private:
    void schedule_block(Clock& clock, EventQueue& q, TransactionSink& sink,
                        Time at_us, Time burst_end_us, Time period_us);

    TextureConfig cfg_;
    std::uint32_t source_inst_;
    QosTag        qos_;
    std::uint64_t next_id_ = 0;
};

class AIWeightInjector {
public:
    AIWeightInjector(const AiWeightsConfig& cfg);

    // Submit a weight prefetch at the given time. lod ∈ {0,1,2}.
    void schedule_prefetch(Clock& clock, EventQueue& q, TransactionSink& sink,
                           Time at_us, std::uint32_t npc_id, int lod);

private:
    AiWeightsConfig cfg_;
    QosTag          qos_;
    std::uint64_t   next_id_ = 0;
};

}
