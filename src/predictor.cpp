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

    Vec2 player_pos = position_at(scenario_.player.waypoints, t_ms);
    for (const auto& npc : scenario_.npcs) {
        Vec2 npc_pos = position_at(npc.waypoints, t_ms);
        double d = distance(player_pos, npc_pos);
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

// ----- IntentPredictor -----

IntentPredictor::IntentPredictor(const Scenario& s, const Config& cfg)
    : scenario_(s), lm_(cfg.lod_manager), ip_(cfg.intent_predictor),
      weights_cfg_(cfg.ai_weights) {
    tick_us_       = lm_.tick_us;
    look_ahead_ms_ = s.intent_look_ahead_ms_override.value_or(ip_.look_ahead_ms);
    fov_deg_       = s.intent_fov_deg_override.value_or(ip_.fov_deg);
    npu_cores_     = cfg.npu.cores;
    slots_.assign(static_cast<std::size_t>(npu_cores_), SlotState{});

    auto capacity_bytes = static_cast<std::uint32_t>(cfg.npu.shared_cache_mb) *
                          1024u * 1024u;
    cache_ = std::make_unique<NpuCache>(
        capacity_bytes, std::make_unique<DistanceLruPolicy>());
}

void IntentPredictor::start(Clock& clock, EventQueue& q,
                            AIWeightInjector& weights, TransactionSink& sink) {
    clock_ = &clock;
    q.schedule(0, [this, &clock, &q, &weights, &sink] {
        tick(clock, q, weights, sink);
    });
}

int IntentPredictor::weight_size_bytes_for_lod(int lod) const {
    int mb = (lod == 0 ? weights_cfg_.lod0_mb
            : lod == 1 ? weights_cfg_.lod1_mb
                       : weights_cfg_.lod2_mb);
    return mb * 1024 * 1024;
}

void IntentPredictor::emit_event(EventType ev, std::uint32_t npc_id, int lod,
                                 std::uint32_t size_bytes) {
    if (!trace_ || !clock_) return;
    TraceRecord r{};
    r.timestamp_us = clock_->now();
    // Encode (kind=Weight, npc_id) like the injector does so analyzers can
    // attribute back to a specific NPC and LOD via size.
    r.source_id = (static_cast<std::uint32_t>(SourceKind::Weight) << 24) |
                  (npc_id & 0xFFFFFFu);
    r.event_type = ev;
    r.qos_tag    = QosTag::High;
    r.dma_path   = DmaPath::None;
    r.size_bytes = size_bytes;
    r.latency_us = static_cast<std::uint32_t>(lod);   // re-use latency slot for LOD info
    r.transaction_id = 0;
    trace_->write(r);
}

int IntentPredictor::best_resident_lod(std::uint32_t npc_id, int desired) const {
    // Prefer the desired LOD; otherwise pick the closest available LOD with
    // a LARGER number (smaller / less detailed model) — that's the
    // "graceful degradation" direction.
    for (int lod = desired; lod <= 2; ++lod) {
        if (cache_->is_resident(CacheKey{npc_id, static_cast<std::uint8_t>(lod)})) {
            return lod;
        }
    }
    return -1;
}

int IntentPredictor::acquire_slot(std::uint32_t npc_id, int lod, int ends_at_ms) {
    // Re-use slot if the same NPC is already in one (idempotent).
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        auto& sl = slots_[i];
        if (sl.occupied && sl.npc_id == npc_id) {
            sl.lod = lod;
            sl.ends_at_ms = std::max(sl.ends_at_ms, ends_at_ms);
            return static_cast<int>(i);
        }
    }
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        if (!slots_[i].occupied) {
            slots_[i] = {true, npc_id, lod, ends_at_ms};
            return static_cast<int>(i);
        }
    }
    return -1;   // saturated
}

void IntentPredictor::release_expired_slots(int t_ms) {
    for (auto& sl : slots_) {
        if (sl.occupied && t_ms >= sl.ends_at_ms) {
            CacheKey k{sl.npc_id, static_cast<std::uint8_t>(sl.lod)};
            cache_->unpin(k);
            sl.occupied = false;
        }
    }
}

void IntentPredictor::handle_interaction(std::uint32_t npc_id, double distance_m,
                                         int desired_lod, int ends_at_ms) {
    // Try to use the desired LOD. If not resident, degrade.
    int actual_lod = best_resident_lod(npc_id, desired_lod);

    if (actual_lod < 0) {
        // Total miss: no usable LOD in cache. Frame must continue.
        emit_event(EventType::Degrade, npc_id, -1, 0);
        kpi_.degradations++;
        kpi_.degradations_no_weight++;
        kpi_.cache_misses++;
        return;
    }

    if (actual_lod != desired_lod) {
        // Partial miss: degraded to a lower-detail tier.
        emit_event(EventType::Degrade, npc_id, actual_lod,
                   static_cast<std::uint32_t>(weight_size_bytes_for_lod(actual_lod)));
        kpi_.degradations++;
        kpi_.cache_misses++;
    } else {
        kpi_.cache_hits++;
    }

    // Pin and acquire a core slot.
    CacheKey k{npc_id, static_cast<std::uint8_t>(actual_lod)};
    int slot = acquire_slot(npc_id, actual_lod, ends_at_ms);
    if (slot < 0) {
        // All cores busy — log saturation but don't fail; the higher-level
        // system would queue this. We track the event and skip pinning.
        emit_event(EventType::Degrade, npc_id, -1, 0);
        kpi_.core_saturation_events++;
        return;
    }
    cache_->pin(k);
    cache_->touch(k, clock_ ? clock_->now() : 0, distance_m);
}

bool IntentPredictor::is_in_frustum(const Vec2& player_pos, double player_facing_deg,
                                    const Vec2& npc_pos) const {
    double dx = npc_pos.x - player_pos.x;
    double dy = npc_pos.y - player_pos.y;
    double d  = std::sqrt(dx * dx + dy * dy);
    if (d < 1e-9) return true;   // NPC stepped onto player — treat as visible
    double rad = player_facing_deg * M_PI / 180.0;
    double fx = std::cos(rad);
    double fy = std::sin(rad);
    double dot = (fx * dx + fy * dy) / d;     // cosine of angle to NPC
    double half_fov_rad = (fov_deg_ * 0.5) * M_PI / 180.0;
    return dot >= std::cos(half_fov_rad);
}

IntentPredictor::NpcSnapshot
IntentPredictor::make_snapshot(const NpcSpec& npc, int t_ms,
                               const Vec2& player_pos, const Vec2& player_vel,
                               double player_facing_deg) const {
    NpcSnapshot s;
    s.npc = &npc;
    Vec2 npc_pos = position_at(npc.waypoints, t_ms);
    Vec2 npc_vel = velocity_at(npc.waypoints, t_ms);

    // Current distance
    double cur_d = distance(player_pos, npc_pos);

    // Future positions after look_ahead_ms
    Vec2 p_future{ player_pos.x + player_vel.x * look_ahead_ms_,
                   player_pos.y + player_vel.y * look_ahead_ms_ };
    Vec2 n_future{ npc_pos.x    + npc_vel.x   * look_ahead_ms_,
                   npc_pos.y    + npc_vel.y   * look_ahead_ms_ };
    double future_d = distance(p_future, n_future);

    // Cascade rules use the *closer* of current vs predicted distance — this
    // makes the predictor preemptive when convergence is detected.
    s.distance_m = std::min(cur_d, future_d);

    s.in_frustum    = is_in_frustum(player_pos, player_facing_deg, npc_pos);
    s.approaching   = (future_d < cur_d - 0.1);     // 0.1m epsilon to ignore noise
    s.quest         = (npc.priority == "quest");
    s.interact_active = false;  // filled by tick() — depends on current time
    return s;
}

int IntentPredictor::select_lod(const NpcSnapshot& s, bool player_stopped) const {
    // Rule 1: quest NPC → always LOD0
    if (s.quest) return 0;

    // Rule 2: active interaction → LOD0
    if (s.interact_active) return 0;

    // Rule 3: stopped near, in frustum → LOD0
    if (player_stopped &&
        s.in_frustum &&
        s.distance_m <= ip_.stopped_dist_m) return 0;

    // Rule 4: in frustum, close → LOD1
    if (s.in_frustum && s.distance_m <= static_cast<double>(ip_.close_m)) return 1;

    // Rule 5: in frustum, near, and (player or NPC) approaching → LOD1
    if (s.in_frustum && s.distance_m <= static_cast<double>(ip_.near_m) && s.approaching)
        return 1;

    // Rule 6: in frustum, near → LOD2
    if (s.in_frustum && s.distance_m <= static_cast<double>(ip_.near_m)) return 2;

    // Rule 7: visible (frustum or not) → LOD2
    if (s.distance_m <= static_cast<double>(ip_.visible_m)) return 2;

    // Rule 8: out of range
    return -1;
}

void IntentPredictor::tick(Clock& clock, EventQueue& q,
                           AIWeightInjector& weights, TransactionSink& sink) {
    int t_ms = static_cast<int>(clock.now() / 1000);

    release_expired_slots(t_ms);

    Vec2 player_pos      = position_at(scenario_.player.waypoints, t_ms);
    Vec2 player_vel      = velocity_at(scenario_.player.waypoints, t_ms);
    double player_facing = facing_at(scenario_.player.waypoints, t_ms);

    double player_speed = std::sqrt(player_vel.x * player_vel.x +
                                    player_vel.y * player_vel.y) * 1000.0;
    bool player_stopped = player_speed < ip_.stopped_speed_m_s;

    for (const auto& npc : scenario_.npcs) {
        auto snap = make_snapshot(npc, t_ms, player_pos, player_vel, player_facing);

        // Check active interactions
        const InteractionSpec* active_ix = nullptr;
        for (const auto& ix : scenario_.interactions) {
            if (ix.npc_id != npc.id) continue;
            if (t_ms >= ix.at_ms && t_ms < ix.at_ms + ix.duration_ms) {
                snap.interact_active = true;
                active_ix = &ix;
                break;
            }
        }

        int desired = select_lod(snap, player_stopped);
        last_lod_[npc.id] = desired;

        if (desired < 0) continue;

        CacheKey ck{npc.id, static_cast<std::uint8_t>(desired)};
        // Phase 8: refresh distance for resident entries so distance-LRU
        // sees up-to-date scores.
        if (cache_->is_resident(ck)) {
            cache_->touch(ck, clock.now(), snap.distance_m);
        }

        // Phase 8: on the first tick of an interaction, attempt to satisfy
        // it from the cache. If unavailable, degrade (and still prefetch).
        if (active_ix) {
            auto ik = key(npc.id, static_cast<int>(active_ix->at_ms));
            if (interact_handled_.insert(ik).second) {
                handle_interaction(npc.id, snap.distance_m, desired,
                                   active_ix->at_ms + active_ix->duration_ms);
            }
        }

        // Decide whether to issue a prefetch.
        auto k = key(npc.id, desired);
        if (in_flight_.count(k) > 0) continue;
        if (cache_->is_resident(ck))  continue;

        in_flight_.insert(k);
        weights.schedule_prefetch(clock, q, sink, clock.now(), npc.id, desired);
    }

    Time next = clock.now() + static_cast<Time>(tick_us_);
    Time end  = static_cast<Time>(scenario_.duration_ms) * 1000;
    if (next < end) {
        q.schedule(next, [this, &clock, &q, &weights, &sink] {
            tick(clock, q, weights, sink);
        });
    }
}

void IntentPredictor::on_complete(std::uint32_t npc_id, int lod) {
    auto k = key(npc_id, lod);
    in_flight_.erase(k);

    // Determine current distance (best-effort — uses last known player /
    // npc positions at completion time).
    int t_ms = clock_ ? static_cast<int>(clock_->now() / 1000) : 0;
    Vec2 player_pos = scenario_.player.waypoints.empty()
                    ? Vec2{0, 0}
                    : position_at(scenario_.player.waypoints, t_ms);
    double dist = 0.0;
    for (const auto& n : scenario_.npcs) {
        if (n.id == npc_id) {
            dist = distance(player_pos, position_at(n.waypoints, t_ms));
            break;
        }
    }

    CacheKey ck{npc_id, static_cast<std::uint8_t>(lod)};
    auto size_bytes = static_cast<std::uint32_t>(weight_size_bytes_for_lod(lod));
    auto res = cache_->admit(ck, size_bytes,
                             clock_ ? clock_->now() : 0, dist);
    for (const auto& evicted : res.evicted) {
        emit_event(EventType::Evict, evicted.npc_id, evicted.lod, 0);
        kpi_.evictions++;
    }
    if (!res.accepted) {
        kpi_.admission_refusals++;
    }
}

// ----- factory -----

std::unique_ptr<Predictor> make_predictor(const std::string& policy,
                                          const Scenario& s, const Config& cfg) {
    if (policy == "scripted") return std::make_unique<ScriptedPredictor>(s);
    if (policy == "lod")      return std::make_unique<LodPredictor>(s, cfg);
    if (policy == "intent")   return std::make_unique<IntentPredictor>(s, cfg);
    throw std::invalid_argument("unknown predictor policy: " + policy);
}

}
