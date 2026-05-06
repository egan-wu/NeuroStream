#include "neurostream/injector.hpp"
#include <cmath>
#include <stdexcept>

namespace neurostream {

QosTag parse_priority(const std::string& s) {
    if (s == "critical") return QosTag::Critical;
    if (s == "high")     return QosTag::High;
    if (s == "normal")   return QosTag::Normal;
    throw std::invalid_argument("unknown priority: " + s);
}

// ---- Audio ----

AudioTrafficGen::AudioTrafficGen(const AudioConfig& cfg, std::uint32_t source_inst)
    : cfg_(cfg), source_inst_(source_inst), qos_(parse_priority(cfg.priority)) {
    // period_us = packet_bytes / rate_mbps  (MB/s × 1e6 cancels with s→μs)
    double p = static_cast<double>(cfg_.packet_bytes) / static_cast<double>(cfg_.rate_mbps);
    period_us_ = static_cast<Time>(std::llround(p));
    if (period_us_ <= 0) period_us_ = 1;
}

void AudioTrafficGen::start(Clock& clock, EventQueue& q, TransactionSink& sink, Time stop_at_us) {
    schedule_next(clock, q, sink, stop_at_us);
}

void AudioTrafficGen::schedule_next(Clock& clock, EventQueue& q, TransactionSink& sink, Time stop_at_us) {
    Time when = clock.now() + period_us_;
    if (when >= stop_at_us) return;
    q.schedule(when, [this, &clock, &q, &sink, stop_at_us] {
        Transaction t;
        t.id          = ++next_id_;
        t.source      = SourceKind::Audio;
        t.source_inst = source_inst_;
        t.qos         = qos_;
        t.size_bytes  = static_cast<std::uint32_t>(cfg_.packet_bytes);
        t.issued_at   = clock.now();
        t.deadline    = clock.now() + cfg_.deadline_us;
        sink.accept(t);
        schedule_next(clock, q, sink, stop_at_us);
    });
}

// ---- Texture ----

TextureTrafficGen::TextureTrafficGen(const TextureConfig& cfg, std::uint32_t source_inst)
    : cfg_(cfg), source_inst_(source_inst), qos_(parse_priority(cfg.priority)) {}

void TextureTrafficGen::schedule_burst(Clock& clock, EventQueue& q, TransactionSink& sink,
                                       Time start_at_us, int rate_mbps, int duration_ms) {
    if (rate_mbps <= 0 || duration_ms <= 0) return;
    double p = static_cast<double>(cfg_.block_bytes) / static_cast<double>(rate_mbps);
    Time period_us  = static_cast<Time>(std::llround(p));
    if (period_us <= 0) period_us = 1;
    Time end_us     = start_at_us + static_cast<Time>(duration_ms) * 1000;
    schedule_block(clock, q, sink, start_at_us, end_us, period_us);
}

void TextureTrafficGen::schedule_block(Clock& clock, EventQueue& q, TransactionSink& sink,
                                       Time at_us, Time burst_end_us, Time period_us) {
    if (at_us >= burst_end_us) return;
    q.schedule(at_us, [this, &clock, &q, &sink, at_us, burst_end_us, period_us] {
        Transaction t;
        t.id          = ++next_id_;
        t.source      = SourceKind::Texture;
        t.source_inst = source_inst_;
        t.qos         = qos_;
        t.size_bytes  = static_cast<std::uint32_t>(cfg_.block_bytes);
        t.issued_at   = clock.now();
        t.deadline    = 0;
        sink.accept(t);
        schedule_block(clock, q, sink, at_us + period_us, burst_end_us, period_us);
    });
}

// ---- AI Weight ----

AIWeightInjector::AIWeightInjector(const AiWeightsConfig& cfg)
    : cfg_(cfg), qos_(parse_priority(cfg.priority)) {}

void AIWeightInjector::schedule_prefetch(Clock& clock, EventQueue& q, TransactionSink& sink,
                                         Time at_us, std::uint32_t npc_id, int lod) {
    int size_mb;
    switch (lod) {
        case 0: size_mb = cfg_.lod0_mb; break;
        case 1: size_mb = cfg_.lod1_mb; break;
        case 2: size_mb = cfg_.lod2_mb; break;
        default: throw std::invalid_argument("lod must be 0, 1, or 2");
    }
    auto size_bytes = static_cast<std::uint32_t>(size_mb) * 1024u * 1024u;

    q.schedule(at_us, [this, &clock, &sink, npc_id, size_bytes] {
        Transaction t;
        t.id          = ++next_id_;
        t.source      = SourceKind::Weight;
        t.source_inst = npc_id;
        t.qos         = qos_;
        t.size_bytes  = size_bytes;
        t.issued_at   = clock.now();
        t.deadline    = 0;
        sink.accept(t);
    });
}

}
