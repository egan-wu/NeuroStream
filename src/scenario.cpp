#include "neurostream/scenario.hpp"
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

    if (auto pf = root["weight_prefetches"]; pf && pf.IsSequence()) {
        for (std::size_t i = 0; i < pf.size(); ++i) {
            YAML::Node p = pf[i];
            std::string ctx = "weight_prefetches[" + std::to_string(i) + "]";
            WeightPrefetchSpec spec;
            spec.at_ms  = as<int>(p, "at_ms", ctx);
            spec.npc_id = as<std::uint32_t>(p, "npc_id", ctx);
            spec.lod    = as<int>(p, "lod", ctx);
            if (spec.lod < 0 || spec.lod > 2) {
                throw ScenarioError(ctx + ".lod must be 0, 1, or 2");
            }
            s.weight_prefetches.push_back(spec);
        }
    }

    return s;
}

}
