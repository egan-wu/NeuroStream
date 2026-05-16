#pragma once
#include "neurostream/clock.hpp"
#include "neurostream/config.hpp"
#include "neurostream/event_queue.hpp"
#include "neurostream/injector.hpp"
#include "neurostream/scenario.hpp"
#include "neurostream/transaction.hpp"
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace neurostream {

class Predictor {
public:
    virtual ~Predictor() = default;
    virtual void start(Clock& clock, EventQueue& q,
                       AIWeightInjector& weights, TransactionSink& sink) = 0;
    // Called when a weight transaction completes; lets the predictor track
    // residency. Default is no-op for baseline predictors.
    virtual void on_complete(std::uint32_t /*npc_id*/, int /*lod*/) {}
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

// Phase 6: intent-aware predictor. Uses 2D positions + frustum + velocity
// look-ahead + interactions + quest priority. Implements a hierarchical
// rule cascade — first matching rule wins. Thresholds in IntentPredictorConfig.
class IntentPredictor : public Predictor {
public:
    IntentPredictor(const Scenario& s, const Config& cfg);
    void start(Clock& clock, EventQueue& q,
               AIWeightInjector& weights, TransactionSink& sink) override;
    void on_complete(std::uint32_t npc_id, int lod) override;

    bool is_resident(std::uint32_t npc_id, int lod) const;
    void on_evict(std::uint32_t npc_id, int lod);

    // Cascade decision: returns -1 for "no LOD", else 0/1/2. Public for
    // direct unit testing.
    struct NpcSnapshot {
        const NpcSpec* npc;
        double distance_m;
        bool   in_frustum;
        bool   approaching;        // relative velocity component toward player
        bool   interact_active;
        bool   quest;
    };
    int select_lod(const NpcSnapshot& s, bool player_stopped) const;

private:
    void tick(Clock& clock, EventQueue& q,
              AIWeightInjector& weights, TransactionSink& sink);

    NpcSnapshot make_snapshot(const NpcSpec& npc, int t_ms,
                              const Vec2& player_pos, const Vec2& player_vel,
                              double player_facing_deg) const;

    bool is_in_frustum(const Vec2& player_pos, double player_facing_deg,
                       const Vec2& npc_pos) const;

    Scenario              scenario_;
    LodManagerConfig      lm_;
    IntentPredictorConfig ip_;
    int                   tick_us_;
    int                   look_ahead_ms_;
    int                   fov_deg_;

    std::unordered_map<std::uint32_t, int> last_lod_;
    std::unordered_set<std::uint64_t>      in_flight_;
    std::unordered_set<std::uint64_t>      resident_;

    static std::uint64_t key(std::uint32_t npc_id, int lod) {
        return (static_cast<std::uint64_t>(npc_id) << 8) |
               static_cast<std::uint64_t>(static_cast<std::uint8_t>(lod & 0xFF));
    }
};

std::unique_ptr<Predictor> make_predictor(const std::string& policy,
                                          const Scenario& s, const Config& cfg);

}
