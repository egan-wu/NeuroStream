#include "neurostream/scenario.hpp"
#include <algorithm>
#include <yaml-cpp/yaml.h>

namespace neurostream {

namespace {

const YAML::Node require(const YAML::Node& parent, const std::string& key,
                         const std::string& path) {
    YAML::Node n = parent[key];
    if (!n || n.IsNull()) {
        throw ScenarioError("missing scenario field: " + path + "." + key);
    }
    return n;
}

template <typename T>
T as(const YAML::Node& parent, const std::string& key, const std::string& path) {
    auto n = require(parent, key, path);
    try {
        return n.as<T>();
    } catch (const YAML::Exception& e) {
        throw ScenarioError("malformed scenario field " + path + "." + key + ": " + e.what());
    }
}

Vec2 parse_pos(const YAML::Node& n, const std::string& ctx) {
    Vec2 v;
    v.x = as<double>(n, "x", ctx);
    v.y = as<double>(n, "y", ctx);
    return v;
}

void parse_waypoint_list(const YAML::Node& wps_node, const std::string& ctx,
                         std::vector<Waypoint>& out, bool allow_facing) {
    if (!wps_node.IsSequence() || wps_node.size() == 0) {
        throw ScenarioError(ctx + " must be a non-empty sequence");
    }
    for (std::size_t j = 0; j < wps_node.size(); ++j) {
        YAML::Node w = wps_node[j];
        std::string wctx = ctx + "[" + std::to_string(j) + "]";
        Waypoint wp;
        wp.at_ms = as<int>(w, "at_ms", wctx);
        auto pos = require(w, "pos", wctx);
        wp.pos = parse_pos(pos, wctx + ".pos");
        if (allow_facing) {
            if (auto f = w["facing_deg"]; f && !f.IsNull()) {
                wp.facing_deg = f.as<double>();
            }
        } else if (w["facing_deg"]) {
            throw ScenarioError(wctx + ": facing_deg is only valid on player waypoints");
        }
        out.push_back(wp);
    }
    std::sort(out.begin(), out.end(),
              [](const Waypoint& a, const Waypoint& b) { return a.at_ms < b.at_ms; });
}

}

Scenario load_scenario(const std::filesystem::path& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        throw ScenarioError("cannot parse " + path.string() + ": " + e.what());
    }

    // Legacy schema detection — clear migration error before anything else.
    if (root["weight_prefetches"]) {
        throw ScenarioError(
            "scenario uses Phase 2 schema (weight_prefetches). "
            "Migrate to v3: drop weight_prefetches, add player.waypoints, "
            "express NPC positions as 2D 'pos: {x, y}'. See docs/scenario-schema.md.");
    }
    // Phase 4 schema detection — distance_m at npc waypoints.
    if (auto ns = root["npcs"]; ns && ns.IsSequence() && ns.size() > 0) {
        if (auto wps = ns[0]["waypoints"]; wps && wps.IsSequence() && wps.size() > 0) {
            if (wps[0]["distance_m"]) {
                throw ScenarioError(
                    "scenario uses Phase 4 schema (npc waypoints with distance_m). "
                    "Migrate to v3: replace 'distance_m: D' with 'pos: {x: D, y: 0}' "
                    "and add a 'player' section. See docs/scenario-schema.md.");
            }
        }
    }

    Scenario s;
    s.schema_version = as<int>(root, "schema_version", "");
    if (s.schema_version != kScenarioSchemaVersion) {
        throw ScenarioError(
            "scenario schema_version " + std::to_string(s.schema_version) +
            " unsupported; this build requires schema_version " +
            std::to_string(kScenarioSchemaVersion));
    }

    s.name        = as<std::string>(root, "name", "");
    s.duration_ms = as<int>(root, "duration_ms", "");

    auto audio = require(root, "audio", "");
    s.audio_enabled = as<bool>(audio, "enabled", "audio");

    if (auto ip = root["intent_predictor"]; ip && !ip.IsNull()) {
        if (auto f = ip["fov_deg"]; f && !f.IsNull()) {
            s.intent_fov_deg_override = f.as<int>();
        }
        if (auto la = ip["look_ahead_ms"]; la && !la.IsNull()) {
            s.intent_look_ahead_ms_override = la.as<int>();
        }
    }

    auto player = require(root, "player", "");
    auto pwps = require(player, "waypoints", "player");
    parse_waypoint_list(pwps, "player.waypoints", s.player.waypoints,
                        /*allow_facing=*/true);

    if (auto bursts = root["texture_bursts"]; bursts && bursts.IsSequence()) {
        for (std::size_t i = 0; i < bursts.size(); ++i) {
            YAML::Node b = bursts[i];
            std::string ctx = "texture_bursts[" + std::to_string(i) + "]";
            TextureBurstSpec spec;
            spec.at_ms       = as<int>(b, "at_ms", ctx);
            spec.rate_mbps   = as<int>(b, "rate_mbps", ctx);
            spec.duration_ms = as<int>(b, "duration_ms", ctx);
            s.texture_bursts.push_back(spec);
        }
    }

    if (auto ns = root["npcs"]; ns && ns.IsSequence()) {
        for (std::size_t i = 0; i < ns.size(); ++i) {
            YAML::Node n = ns[i];
            std::string ctx = "npcs[" + std::to_string(i) + "]";
            NpcSpec spec;
            spec.id = as<std::uint32_t>(n, "id", ctx);
            if (auto pri = n["priority"]; pri && !pri.IsNull()) {
                spec.priority = pri.as<std::string>();
                if (spec.priority != "normal" && spec.priority != "quest") {
                    throw ScenarioError(ctx + ".priority must be 'normal' or 'quest'");
                }
            }
            auto wps = require(n, "waypoints", ctx);
            parse_waypoint_list(wps, ctx + ".waypoints", spec.waypoints,
                                /*allow_facing=*/false);
            s.npcs.push_back(spec);
        }
    }

    if (auto is = root["interactions"]; is && is.IsSequence()) {
        for (std::size_t i = 0; i < is.size(); ++i) {
            YAML::Node n = is[i];
            std::string ctx = "interactions[" + std::to_string(i) + "]";
            InteractionSpec spec;
            spec.at_ms       = as<int>(n, "at_ms", ctx);
            spec.npc_id      = as<std::uint32_t>(n, "npc_id", ctx);
            spec.duration_ms = as<int>(n, "duration_ms", ctx);
            if (spec.duration_ms <= 0) {
                throw ScenarioError(ctx + ".duration_ms must be > 0");
            }
            s.interactions.push_back(spec);
        }
    }

    return s;
}

// ---- Waypoint helpers ----

namespace {
struct Surround { const Waypoint *a, *b; double t; };

Surround surround_at(const std::vector<Waypoint>& wps, int t_ms) {
    Surround s{nullptr, nullptr, 0.0};
    if (wps.empty()) return s;
    if (t_ms <= wps.front().at_ms) { s.a = s.b = &wps.front(); s.t = 0.0; return s; }
    if (t_ms >= wps.back().at_ms)  { s.a = s.b = &wps.back();  s.t = 0.0; return s; }
    for (std::size_t i = 1; i < wps.size(); ++i) {
        if (t_ms <= wps[i].at_ms) {
            s.a = &wps[i - 1];
            s.b = &wps[i];
            double dt = wps[i].at_ms - wps[i - 1].at_ms;
            s.t = (dt == 0) ? 0.0
                            : static_cast<double>(t_ms - wps[i - 1].at_ms) / dt;
            return s;
        }
    }
    s.a = s.b = &wps.back();
    return s;
}
}

Vec2 position_at(const std::vector<Waypoint>& wps, int t_ms) {
    auto s = surround_at(wps, t_ms);
    if (!s.a) return {0.0, 0.0};
    return {
        s.a->pos.x + s.t * (s.b->pos.x - s.a->pos.x),
        s.a->pos.y + s.t * (s.b->pos.y - s.a->pos.y),
    };
}

Vec2 velocity_at(const std::vector<Waypoint>& wps, int t_ms) {
    if (wps.size() < 2) return {0.0, 0.0};
    auto s = surround_at(wps, t_ms);
    if (!s.a || s.a == s.b) {
        // At/past the last waypoint — use trailing slope from the last
        // two waypoints.
        if (wps.size() < 2) return {0.0, 0.0};
        const auto& a = wps[wps.size() - 2];
        const auto& b = wps.back();
        double dt = b.at_ms - a.at_ms;
        if (dt == 0) return {0.0, 0.0};
        return { (b.pos.x - a.pos.x) / dt, (b.pos.y - a.pos.y) / dt };
    }
    double dt = s.b->at_ms - s.a->at_ms;
    if (dt == 0) return {0.0, 0.0};
    return { (s.b->pos.x - s.a->pos.x) / dt, (s.b->pos.y - s.a->pos.y) / dt };
}

double facing_at(const std::vector<Waypoint>& wps, int t_ms) {
    if (wps.empty()) return 0.0;
    // Walk forward, tracking the last known facing.
    double last = 0.0;
    bool   seen = false;
    if (wps.front().facing_deg.has_value()) {
        last = *wps.front().facing_deg;
        seen = true;
    }
    if (t_ms <= wps.front().at_ms) return seen ? last : 0.0;
    if (t_ms >= wps.back().at_ms) {
        for (const auto& w : wps) if (w.facing_deg.has_value()) last = *w.facing_deg;
        return last;
    }
    for (std::size_t i = 1; i < wps.size(); ++i) {
        if (t_ms <= wps[i].at_ms) {
            double prev_f = last;
            double next_f = wps[i].facing_deg.value_or(last);
            if (wps[i - 1].facing_deg.has_value()) prev_f = *wps[i - 1].facing_deg;
            double dt = wps[i].at_ms - wps[i - 1].at_ms;
            double t = (dt == 0) ? 0.0
                                 : static_cast<double>(t_ms - wps[i - 1].at_ms) / dt;
            return prev_f + t * (next_f - prev_f);
        }
        if (wps[i].facing_deg.has_value()) last = *wps[i].facing_deg;
    }
    return last;
}

}
