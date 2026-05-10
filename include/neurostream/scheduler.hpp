#pragma once
#include "neurostream/clock.hpp"
#include "neurostream/config.hpp"
#include "neurostream/event_queue.hpp"
#include "neurostream/trace.hpp"
#include "neurostream/transaction.hpp"
#include <cstdint>
#include <deque>
#include <memory>

namespace neurostream {

// Per-transaction state during scheduling. Lifecycle: Arrived → ServiceStart
// → (Complete | Drop). Carries bytes_remaining for partial-quantum service.
struct PendingTxn {
    Transaction   tx;
    std::uint32_t bytes_remaining;
    Time          arrived_at;
    bool          service_started = false;
};

// Common scheduler base: bus model, KPI counters, milestone trace emission.
// Concrete policies override pick_next() and on_arrive() bookkeeping.
class Scheduler : public TransactionSink {
public:
    Scheduler(const Config& cfg, Clock& clock, EventQueue& q, TraceWriter* trace);
    ~Scheduler() override = default;

    void accept(const Transaction& tx) override;

    // Aggregate KPIs computed during the run (cheap to read at end).
    struct Kpi {
        std::uint64_t issued      = 0;
        std::uint64_t completed   = 0;
        std::uint64_t dropped     = 0;
        std::uint64_t audio_completed = 0;
        std::uint64_t audio_dropped   = 0;
        Time          audio_lat_max   = 0;
        double        audio_lat_mean  = 0.0;
        // P99 needs the full distribution; keep a small reservoir of samples.
        std::vector<Time> audio_lat_samples;
        Time          weight_lat_max  = 0;
    };
    const Kpi& kpi() const noexcept { return kpi_; }

protected:
    virtual std::unique_ptr<PendingTxn> pick_next() = 0;
    virtual void on_arrive(std::unique_ptr<PendingTxn>) = 0;

    // Helpers for derived classes.
    void start_service_if_idle();
    void schedule_quantum_end(PendingTxn* p);
    void emit(EventType, const Transaction&, std::uint32_t latency_us = 0);

    const Config& cfg_;
    Clock&        clock_;
    EventQueue&   q_;
    TraceWriter*  trace_;
    bool          bus_idle_ = true;
    Kpi           kpi_;

private:
    void on_quantum_end(std::unique_ptr<PendingTxn> p);
};

// Baseline: oldest-arrival-first across all classes, no preemption-fairness.
class FIFOScheduler : public Scheduler {
public:
    using Scheduler::Scheduler;

protected:
    std::unique_ptr<PendingTxn> pick_next() override;
    void on_arrive(std::unique_ptr<PendingTxn>) override;

private:
    std::deque<std::unique_ptr<PendingTxn>> queue_;
};

// NeuroStream: strict-priority Critical (rate-limited via token bucket) +
// virtual-time weighted-fair queueing for High and Normal bulk classes.
class QoSScheduler : public Scheduler {
public:
    QoSScheduler(const Config& cfg, Clock& clock, EventQueue& q, TraceWriter* trace);

protected:
    std::unique_ptr<PendingTxn> pick_next() override;
    void on_arrive(std::unique_ptr<PendingTxn>) override;

private:
    std::deque<std::unique_ptr<PendingTxn>> q_critical_;
    std::deque<std::unique_ptr<PendingTxn>> q_high_;
    std::deque<std::unique_ptr<PendingTxn>> q_normal_;

    // Token bucket for critical rate limit.
    double tokens_bytes_;
    Time   last_refill_us_;
    double token_capacity_bytes_;
    double token_refill_per_us_;

    // Virtual-time fair queueing across bulk classes.
    double vfin_high_   = 0.0;
    double vfin_normal_ = 0.0;

    void refill_tokens();
};

std::unique_ptr<Scheduler> make_scheduler(const Config& cfg, Clock& clock,
                                          EventQueue& q, TraceWriter* trace);

}
