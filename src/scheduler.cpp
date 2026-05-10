#include "neurostream/scheduler.hpp"
#include <algorithm>
#include <stdexcept>

namespace neurostream {

namespace {
// Bytes that can be served in `us` microseconds at `mbps` MB/s.
// MB/s × μs == bytes (because 1e6 μs = 1 s and 1 MB = 1e6 B).
inline std::uint64_t bytes_for(int mbps, Time us) {
    return static_cast<std::uint64_t>(mbps) * static_cast<std::uint64_t>(us);
}
}

Scheduler::Scheduler(const Config& cfg, Clock& clock, EventQueue& q, TraceWriter* trace)
    : cfg_(cfg), clock_(clock), q_(q), trace_(trace) {}

void Scheduler::emit(EventType ev, const Transaction& tx, std::uint32_t latency_us) {
    if (!trace_) return;
    TraceRecord r{};
    r.timestamp_us   = clock_.now();
    r.source_id      = static_cast<std::uint32_t>(tx.source) << 24 | (tx.source_inst & 0xFFFFFF);
    r.event_type     = ev;
    r.qos_tag        = tx.qos;
    r.size_bytes     = tx.size_bytes;
    r.latency_us     = latency_us;
    r.transaction_id = tx.id;
    trace_->write(r);
}

void Scheduler::accept(const Transaction& tx) {
    auto p = std::make_unique<PendingTxn>();
    p->tx              = tx;
    p->bytes_remaining = tx.size_bytes;
    p->arrived_at      = clock_.now();
    kpi_.issued++;
    emit(EventType::Arrive, tx);
    on_arrive(std::move(p));
    start_service_if_idle();
}

void Scheduler::start_service_if_idle() {
    if (!bus_idle_) return;
    auto p = pick_next();
    if (!p) return;
    bus_idle_ = false;

    if (!p->service_started) {
        p->service_started = true;
        emit(EventType::ServiceStart, p->tx,
             static_cast<std::uint32_t>(clock_.now() - p->arrived_at));
    }
    schedule_quantum_end(p.release());
}

void Scheduler::schedule_quantum_end(PendingTxn* p_raw) {
    std::unique_ptr<PendingTxn> p(p_raw);
    int  bandwidth = cfg_.bus.total_bandwidth_mbps;
    Time quantum   = cfg_.scheduler.quantum_us;
    auto quantum_bytes = bytes_for(bandwidth, quantum);

    Time service_us;
    if (p->bytes_remaining <= quantum_bytes) {
        // Completes within this quantum — schedule the actual completion time.
        service_us = static_cast<Time>(p->bytes_remaining) / bandwidth;
        if (service_us == 0) service_us = 1;  // round up to 1 μs
    } else {
        service_us = quantum;
    }

    Time fire_at = clock_.now() + service_us;
    auto raw = p.release();
    q_.schedule(fire_at, [this, raw] {
        std::unique_ptr<PendingTxn> p(raw);
        on_quantum_end(std::move(p));
    });
}

void Scheduler::on_quantum_end(std::unique_ptr<PendingTxn> p) {
    int  bandwidth = cfg_.bus.total_bandwidth_mbps;
    Time quantum   = cfg_.scheduler.quantum_us;
    auto quantum_bytes = bytes_for(bandwidth, quantum);

    auto served = std::min<std::uint64_t>(p->bytes_remaining, quantum_bytes);
    p->bytes_remaining -= static_cast<std::uint32_t>(served);

    if (p->bytes_remaining == 0) {
        // Done.
        Time e2e = clock_.now() - p->tx.issued_at;
        emit(EventType::Complete, p->tx, static_cast<std::uint32_t>(e2e));
        kpi_.completed++;
        if (p->tx.source == SourceKind::Audio) {
            kpi_.audio_completed++;
            kpi_.audio_lat_max = std::max(kpi_.audio_lat_max, e2e);
            // streaming mean
            kpi_.audio_lat_mean +=
                (static_cast<double>(e2e) - kpi_.audio_lat_mean) /
                static_cast<double>(kpi_.audio_completed);
            kpi_.audio_lat_samples.push_back(e2e);
        } else if (p->tx.source == SourceKind::Weight) {
            kpi_.weight_lat_max = std::max(kpi_.weight_lat_max, e2e);
        }
    } else {
        // Re-queue. Drop check: audio past deadline never gets back into queue.
        if (p->tx.deadline > 0 && clock_.now() >= p->tx.deadline) {
            emit(EventType::Drop, p->tx);
            kpi_.dropped++;
            if (p->tx.source == SourceKind::Audio) kpi_.audio_dropped++;
        } else {
            on_arrive(std::move(p));
        }
    }
    bus_idle_ = true;
    start_service_if_idle();
}

// ---- FIFO ----

void FIFOScheduler::on_arrive(std::unique_ptr<PendingTxn> p) {
    queue_.push_back(std::move(p));
}

std::unique_ptr<PendingTxn> FIFOScheduler::pick_next() {
    // Drop expired audio before picking.
    while (!queue_.empty()) {
        auto& front = queue_.front();
        if (front->tx.deadline > 0 && clock_.now() >= front->tx.deadline
            && !front->service_started) {
            emit(EventType::Drop, front->tx);
            kpi_.dropped++;
            if (front->tx.source == SourceKind::Audio) kpi_.audio_dropped++;
            queue_.pop_front();
            continue;
        }
        break;
    }
    if (queue_.empty()) return nullptr;
    auto p = std::move(queue_.front());
    queue_.pop_front();
    return p;
}

// ---- QoS ----

QoSScheduler::QoSScheduler(const Config& cfg, Clock& clock, EventQueue& q, TraceWriter* trace)
    : Scheduler(cfg, clock, q, trace) {
    double bw_bytes_per_us = static_cast<double>(cfg.bus.total_bandwidth_mbps);
    double pct             = static_cast<double>(cfg.scheduler.critical_rate_limit_pct) / 100.0;
    token_refill_per_us_   = bw_bytes_per_us * pct;
    // Capacity = 10 ms of budget. Lets a short audio burst pass without
    // throttling, but caps a misconfigured Critical flow at the steady rate.
    token_capacity_bytes_  = token_refill_per_us_ * 10000.0;
    tokens_bytes_          = token_capacity_bytes_;
    last_refill_us_        = 0;
}

void QoSScheduler::refill_tokens() {
    Time now = clock_.now();
    Time dt  = now - last_refill_us_;
    if (dt > 0) {
        tokens_bytes_ = std::min(token_capacity_bytes_,
                                 tokens_bytes_ + static_cast<double>(dt) * token_refill_per_us_);
        last_refill_us_ = now;
    }
}

void QoSScheduler::on_arrive(std::unique_ptr<PendingTxn> p) {
    switch (p->tx.qos) {
        case QosTag::Critical: q_critical_.push_back(std::move(p)); break;
        case QosTag::High:     q_high_.push_back(std::move(p));     break;
        case QosTag::Normal:   q_normal_.push_back(std::move(p));   break;
    }
}

std::unique_ptr<PendingTxn> QoSScheduler::pick_next() {
    refill_tokens();

    // Drop expired critical first (only audio has deadline today).
    while (!q_critical_.empty()) {
        auto& f = q_critical_.front();
        if (f->tx.deadline > 0 && clock_.now() >= f->tx.deadline && !f->service_started) {
            emit(EventType::Drop, f->tx);
            kpi_.dropped++;
            if (f->tx.source == SourceKind::Audio) kpi_.audio_dropped++;
            q_critical_.pop_front();
            continue;
        }
        break;
    }

    // Strict priority: Critical wins if it has tokens to spend.
    if (!q_critical_.empty()) {
        auto& f = q_critical_.front();
        double cost = static_cast<double>(std::min<std::uint32_t>(
            f->bytes_remaining,
            static_cast<std::uint32_t>(bytes_for(cfg_.bus.total_bandwidth_mbps, cfg_.scheduler.quantum_us))));
        if (tokens_bytes_ >= cost) {
            tokens_bytes_ -= cost;
            auto p = std::move(q_critical_.front());
            q_critical_.pop_front();
            return p;
        }
        // Out of tokens — fall through to bulk; critical waits for refill.
    }

    // Bulk: virtual-time weighted-fair. Pick class with smallest finish time.
    bool has_h = !q_high_.empty();
    bool has_n = !q_normal_.empty();
    if (!has_h && !has_n) return nullptr;

    bool pick_high;
    if (has_h && !has_n) pick_high = true;
    else if (has_n && !has_h) pick_high = false;
    else pick_high = (vfin_high_ <= vfin_normal_);

    auto& q     = pick_high ? q_high_ : q_normal_;
    int   w     = pick_high ? cfg_.scheduler.bulk_weights.high : cfg_.scheduler.bulk_weights.normal;
    auto& vfin  = pick_high ? vfin_high_ : vfin_normal_;
    auto  p     = std::move(q.front()); q.pop_front();

    auto cost_bytes = std::min<std::uint64_t>(
        p->bytes_remaining,
        bytes_for(cfg_.bus.total_bandwidth_mbps, cfg_.scheduler.quantum_us));
    vfin += static_cast<double>(cost_bytes) / std::max(1, w);
    return p;
}

// ---- Factory ----

std::unique_ptr<Scheduler> make_scheduler(const Config& cfg, Clock& clock,
                                          EventQueue& q, TraceWriter* trace) {
    if (cfg.scheduler.policy == "fifo") {
        return std::make_unique<FIFOScheduler>(cfg, clock, q, trace);
    }
    if (cfg.scheduler.policy == "qos") {
        return std::make_unique<QoSScheduler>(cfg, clock, q, trace);
    }
    throw std::invalid_argument("unknown scheduler policy: " + cfg.scheduler.policy);
}

}
