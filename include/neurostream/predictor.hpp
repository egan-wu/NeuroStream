#pragma once
#include "neurostream/clock.hpp"
#include "neurostream/config.hpp"
#include "neurostream/event_queue.hpp"
#include "neurostream/injector.hpp"
#include "neurostream/npu_cache.hpp"
#include "neurostream/scenario.hpp"
#include "neurostream/trace.hpp"
#include "neurostream/transaction.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace neurostream {

// Phase 8 KPI counters reported by predictors that maintain a cache.
struct PredictorKpi {
    std::uint64_t cache_hits           = 0;
    std::uint64_t cache_misses         = 0;
    std::uint64_t evictions            = 0;
    std::uint64_t admission_refusals   = 0;
    std::uint64_t degradations         = 0;       // total fallbacks
    std::uint64_t degradations_no_weight = 0;     // no LOD at all
    std::uint64_t core_saturation_events = 0;
};

class Predictor {
public:
    virtual ~Predictor() = default;
    virtual void start(Clock& clock, EventQueue& q,
                       AIWeightInjector& weights, TransactionSink& sink) = 0;
    // Called when a weight transaction completes; lets the predictor track
    // residency. Default is no-op for baseline predictors.
    virtual void on_complete(std::uint32_t /*npc_id*/, int /*lod*/) {}
    // Allow main loop to attach a trace writer so Phase 8 events can be
    // recorded under the same trace file as the scheduler events.
    virtual void set_trace_writer(class TraceWriter* /*tw*/) {}
    virtual const PredictorKpi& kpi() const {
        static const PredictorKpi empty;
        return empty;
    }
};

// Baseline: load LOD0 for every NPC at scenario start, ignore distance and
// motion. Represents the "no LOD intelligence" control group for the 2×2 A/B
// (scripted × {fifo, qos}, lod × {fifo, qos}).
class ScriptedPredictor : public Predictor {
public:
    explicit ScriptedPredictor(const Scenario& s) : scenario_(s) {}
    void start(Clock& clock, EventQueue& q,
               AIWeightInjector& weights, TransactionSink& sink) override;

private:
    Scenario scenario_;
};

// Phase 4 distance-driven predictor with hysteresis and in-flight tracking.
// Phase 8 will implement bounded cache with eviction behind the same
// on_complete / is_resident / on_evict interface.
class LodPredictor : public Predictor {
public:
    LodPredictor(const Scenario& s, const Config& cfg);
    void start(Clock& clock, EventQueue& q,
               AIWeightInjector& weights, TransactionSink& sink) override;
    void on_complete(std::uint32_t npc_id, int lod) override;

    // Cache interface (Phase 4: unbounded resident set; Phase 8: bounded LRU).
    bool is_resident(std::uint32_t npc_id, int lod) const;
    void on_evict(std::uint32_t npc_id, int lod);

    // Decide what LOD this NPC should use given distance and previous LOD
    // (for hysteresis). Returns -1 for "no LOD" (beyond all bands).
    // last_lod: -2 = uninitialized (cold start), else previous decision.
    // Public for unit testing.
    int select_lod(double distance_m, int last_lod) const;

private:
    void tick(Clock& clock, EventQueue& q,
              AIWeightInjector& weights, TransactionSink& sink);

    Scenario          scenario_;
    LodManagerConfig  lm_;

    // Per-NPC last-decided LOD; -2 means uninitialized (cold start).
    std::unordered_map<std::uint32_t, int> last_lod_;

    // (npc_id, lod) pairs currently being fetched. Suppresses duplicates.
    std::unordered_set<std::uint64_t> in_flight_;
    // (npc_id, lod) pairs assumed resident. Phase 4 unbounded; Phase 8 evicts.
    std::unordered_set<std::uint64_t> resident_;

    static std::uint64_t key(std::uint32_t npc_id, int lod) {
        return (static_cast<std::uint64_t>(npc_id) << 8) |
               static_cast<std::uint64_t>(static_cast<std::uint8_t>(lod & 0xFF));
    }
};

// Phase 6: intent-aware predictor with cascade rules and frustum look-ahead.
// Phase 8: integrates bounded NpuCache, distance-LRU eviction, multi-core
// pinning, and graceful degradation on cache miss.
class IntentPredictor : public Predictor {
public:
    IntentPredictor(const Scenario& s, const Config& cfg);
    void start(Clock& clock, EventQueue& q,
               AIWeightInjector& weights, TransactionSink& sink) override;
    void on_complete(std::uint32_t npc_id, int lod) override;
    void set_trace_writer(TraceWriter* tw) override { trace_ = tw; }
    const PredictorKpi& kpi() const override { return kpi_; }

    // Cascade decision snapshot. Returns -1 (no LOD), else 0/1/2.
    struct NpcSnapshot {
        const NpcSpec* npc;
        double distance_m;
        bool   in_frustum;
        bool   approaching;
        bool   interact_active;
        bool   quest;
    };
    int select_lod(const NpcSnapshot& s, bool player_stopped) const;

    // Test introspection.
    const NpuCache& cache() const { return *cache_; }

private:
    void tick(Clock& clock, EventQueue& q,
              AIWeightInjector& weights, TransactionSink& sink);

    NpcSnapshot make_snapshot(const NpcSpec& npc, int t_ms,
                              const Vec2& player_pos, const Vec2& player_vel,
                              double player_facing_deg) const;

    bool is_in_frustum(const Vec2& player_pos, double player_facing_deg,
                       const Vec2& npc_pos) const;

    // Phase 8 helpers.
    void emit_event(EventType ev, std::uint32_t npc_id, int lod,
                    std::uint32_t size_bytes);
    int  best_resident_lod(std::uint32_t npc_id, int desired) const;
    int  weight_size_bytes_for_lod(int lod) const;
    int  acquire_slot(std::uint32_t npc_id, int lod, int ends_at_ms);
    void release_expired_slots(int t_ms);
    void handle_interaction(std::uint32_t npc_id, double distance_m,
                            int desired_lod, int ends_at_ms);

    Scenario              scenario_;
    LodManagerConfig      lm_;
    IntentPredictorConfig ip_;
    int                   tick_us_;
    int                   look_ahead_ms_;
    int                   fov_deg_;
    int                   npu_cores_;
    AiWeightsConfig       weights_cfg_;

    TraceWriter*          trace_   = nullptr;
    PredictorKpi          kpi_;
    Clock*                clock_   = nullptr;

    std::unique_ptr<NpuCache>              cache_;
    std::unordered_map<std::uint32_t, int> last_lod_;
    std::unordered_set<std::uint64_t>      in_flight_;
    std::unordered_set<std::uint64_t>      interact_handled_;

    struct SlotState {
        bool          occupied   = false;
        std::uint32_t npc_id     = 0;
        int           lod        = 0;
        int           ends_at_ms = 0;
    };
    std::vector<SlotState> slots_;

    static std::uint64_t key(std::uint32_t npc_id, int lod) {
        return (static_cast<std::uint64_t>(npc_id) << 8) |
               static_cast<std::uint64_t>(static_cast<std::uint8_t>(lod & 0xFF));
    }
};

std::unique_ptr<Predictor> make_predictor(const std::string& policy,
                                          const Scenario& s, const Config& cfg);

}
