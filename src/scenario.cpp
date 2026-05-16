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

}

Scenario load_scenario(const std::filesystem::path& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        throw ScenarioError("cannot parse " + path.string() + ": " + e.what());
    }

    Scenario s;
    s.name        = as<std::string>(root, "name", "");
    s.duration_ms = as<int>(root, "duration_ms", "");

    auto audio = require(root, "audio", "");
    s.audio_enabled = as<bool>(audio, "enabled", "audio");

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

    if (root["weight_prefetches"]) {
        throw ScenarioError(
            "scenario field 'weight_prefetches' is no longer supported; "
            "use 'npcs:' with waypoints (see docs/scenario-schema.md). "
            "Phase 4 migrated to declarative motion.");
    }

    if (auto ns = root["npcs"]; ns && ns.IsSequence()) {
        for (std::size_t i = 0; i < ns.size(); ++i) {
            YAML::Node n = ns[i];
            std::string ctx = "npcs[" + std::to_string(i) + "]";
            NpcSpec spec;
            spec.id = as<std::uint32_t>(n, "id", ctx);

            if (auto pri = n["priority"]; pri && !pri.IsNull()) {
                spec.priority = pri.as<std::string>();
            }

            auto wps = require(n, "waypoints", ctx);
            if (!wps.IsSequence() || wps.size() == 0) {
                throw ScenarioError(ctx + ".waypoints must be a non-empty sequence");
            }
            for (std::size_t j = 0; j < wps.size(); ++j) {
                YAML::Node w = wps[j];
                std::string wctx = ctx + ".waypoints[" + std::to_string(j) + "]";
                Waypoint wp;
                wp.at_ms      = as<int>(w, "at_ms", wctx);
                wp.distance_m = as<double>(w, "distance_m", wctx);
                if (wp.distance_m < 0.0) {
                    throw ScenarioError(wctx + ".distance_m must be non-negative");
                }
                // Phase 6 reserved fields — read if present, ignore otherwise.
                if (auto px = w["pos_x"]; px && !px.IsNull()) wp.pos_x = px.as<double>();
                if (auto py = w["pos_y"]; py && !py.IsNull()) wp.pos_y = py.as<double>();
                spec.waypoints.push_back(wp);
            }
            std::sort(spec.waypoints.begin(), spec.waypoints.end(),
                      [](const Waypoint& a, const Waypoint& b) { return a.at_ms < b.at_ms; });
            s.npcs.push_back(spec);
        }
    }

    return s;
}

double distance_at(const std::vector<Waypoint>& wps, int t_ms) {
    if (wps.empty()) return 0.0;
    if (t_ms <= wps.front().at_ms) return wps.front().distance_m;
    if (t_ms >= wps.back().at_ms)  return wps.back().distance_m;
    // Binary-search-free linear scan; waypoint lists are short (<10 typical).
    for (std::size_t i = 1; i < wps.size(); ++i) {
        if (t_ms <= wps[i].at_ms) {
            const auto& a = wps[i - 1];
            const auto& b = wps[i];
            if (b.at_ms == a.at_ms) return b.distance_m;
            double t = static_cast<double>(t_ms - a.at_ms) /
                       static_cast<double>(b.at_ms - a.at_ms);
            return a.distance_m + t * (b.distance_m - a.distance_m);
        }
    }
    return wps.back().distance_m;
}

}
