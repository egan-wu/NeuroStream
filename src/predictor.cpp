#include "neurostream/predictor.hpp"
#include <stdexcept>

namespace neurostream {

// ----- ScriptedPredictor: load LOD0 for every NPC at t=0 -----

void ScriptedPredictor::start(Clock& clock, EventQueue& q,
                              AIWeightInjector& weights, TransactionSink& sink) {
    for (const auto& npc : scenario_.npcs) {
        weights.schedule_prefetch(clock, q, sink, /*at_us=*/0, npc.id, /*lod=*/0);
    }
}

// ----- LodPredictor: distance + hysteresis + in-flight tracking -----

LodPredictor::LodPredictor(const Scenario& s, const Config& cfg)
    : scenario_(s), lm_(cfg.lod_manager) {}

int LodPredictor::select_lod(double distance_m, int last_lod) const {
    const double hys = static_cast<double>(lm_.hysteresis_pct) / 100.0;

    // Cold start: pick the most conservative LOD (largest LOD number whose
    // max_distance still contains us). This avoids starting at LOD0 and
    // immediately downgrading.
    if (last_lod == -2) {
        for (const auto& b : lm_.bands) {
            if (distance_m <= static_cast<double>(b.max_distance_m)) {
                // Take the LAST matching band (largest LOD number).
                int chosen = b.lod;
                for (const auto& b2 : lm_.bands) {
                    if (distance_m <= static_cast<double>(b2.max_distance_m)
                        && b2.lod > chosen) {
                        chosen = b2.lod;
                    }
                }
                return chosen;
            }
        }
        return -1;
    }

    // Returning user: apply hysteresis. The current LOD's "leave" threshold
    // is band.max_distance × (1 + hys). We only LEAVE the current LOD when
    // distance crosses the leave threshold; otherwise we stay.
    // To DECREASE LOD (go closer, smaller number), distance must drop into a
    // tighter band's normal max_distance.

    // Find the band for last_lod.
    const LodBand* cur = nullptr;
    for (const auto& b : lm_.bands) {
        if (b.lod == last_lod) { cur = &b; break; }
    }

    // last_lod was -1 (no LOD beyond all bands).
    if (last_lod == -1) {
        // Re-enter: need distance ≤ largest band's normal max (no hysteresis
        // on the "off → on" transition, treated like cold start).
        for (const auto& b : lm_.bands) {
            if (distance_m <= static_cast<double>(b.max_distance_m)) {
                // Use the largest LOD number band that contains us.
                int chosen = -1;
                for (const auto& b2 : lm_.bands) {
                    if (distance_m <= static_cast<double>(b2.max_distance_m)
                        && b2.lod > chosen) {
                        chosen = b2.lod;
                    }
                }
                return chosen;
            }
        }
        return -1;
    }

    if (cur == nullptr) {
        // last_lod doesn't map to any band — should not happen; behave as
        // cold start.
        return select_lod(distance_m, -2);
    }

    const double leave_far = static_cast<double>(cur->max_distance_m) * (1.0 + hys);

    if (distance_m <= leave_far) {
        // Still within current band's outer deadband — but maybe we should
        // upgrade (move CLOSER) to a smaller LOD number.
        // Find the smallest LOD whose normal max_distance ≥ distance_m.
        int best = cur->lod;
        for (const auto& b : lm_.bands) {
            if (b.lod < cur->lod &&
                distance_m <= static_cast<double>(b.max_distance_m)) {
                if (b.lod < best) best = b.lod;
            }
        }
        return best;
    }

    // Crossed the leave threshold — demote to next band out. Find the band
    // with the smallest max_distance that still contains distance_m.
    int chosen = -1;
    int chosen_max = 0;
    for (const auto& b : lm_.bands) {
        if (b.lod <= cur->lod) continue;       // only consider farther bands
        if (distance_m <= static_cast<double>(b.max_distance_m)) {
            if (chosen == -1 || b.max_distance_m < chosen_max) {
                chosen = b.lod;
                chosen_max = b.max_distance_m;
            }
        }
    }
    return chosen;   // -1 means "beyond all bands" (no LOD)
}

void LodPredictor::start(Clock& clock, EventQueue& q,
                         AIWeightInjector& weights, TransactionSink& sink) {
    // Schedule the first tick at t=0; the tick reschedules itself.
    q.schedule(0, [this, &clock, &q, &weights, &sink] {
        tick(clock, q, weights, sink);
    });
}

void LodPredictor::tick(Clock& clock, EventQueue& q,
                        AIWeightInjector& weights, TransactionSink& sink) {
    int t_ms = static_cast<int>(clock.now() / 1000);

    for (const auto& npc : scenario_.npcs) {
        double d = distance_at(npc.waypoints, t_ms);
        auto it = last_lod_.find(npc.id);
        int prev = (it == last_lod_.end()) ? -2 : it->second;
        int desired = select_lod(d, prev);
        last_lod_[npc.id] = desired;

        if (desired < 0) continue;   // no LOD needed (FSM-only range)

        auto k = key(npc.id, desired);
        if (in_flight_.count(k) > 0) continue;     // already loading
        if (resident_.count(k) > 0) continue;      // already loaded

        in_flight_.insert(k);
        weights.schedule_prefetch(clock, q, sink, clock.now(), npc.id, desired);
    }

    // Re-arm next tick if scenario hasn't ended.
    Time next = clock.now() + static_cast<Time>(lm_.tick_us);
    Time end  = static_cast<Time>(scenario_.duration_ms) * 1000;
    if (next < end) {
        q.schedule(next, [this, &clock, &q, &weights, &sink] {
            tick(clock, q, weights, sink);
        });
    }
}

void LodPredictor::on_complete(std::uint32_t npc_id, int lod) {
    auto k = key(npc_id, lod);
    in_flight_.erase(k);
    resident_.insert(k);
}

bool LodPredictor::is_resident(std::uint32_t npc_id, int lod) const {
    return resident_.count(key(npc_id, lod)) > 0;
}

void LodPredictor::on_evict(std::uint32_t npc_id, int lod) {
    resident_.erase(key(npc_id, lod));
}

// ----- factory -----

std::unique_ptr<Predictor> make_predictor(const std::string& policy,
                                          const Scenario& s, const Config& cfg) {
    if (policy == "scripted") return std::make_unique<ScriptedPredictor>(s);
    if (policy == "lod")      return std::make_unique<LodPredictor>(s, cfg);
    throw std::invalid_argument("unknown predictor policy: " + policy);
}

}
