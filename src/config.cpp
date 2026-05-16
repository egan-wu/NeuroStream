#include "neurostream/config.hpp"
#include <yaml-cpp/yaml.h>
#include <sstream>

namespace neurostream {

namespace {

// require(): fail-fast YAML field access. Keeps a path string so the error
// message names the missing field exactly.
const YAML::Node require(const YAML::Node& parent, const std::string& key,
                         const std::string& path) {
    YAML::Node n = parent[key];
    if (!n || n.IsNull()) {
        throw ConfigError("missing config field: " + path + "." + key);
    }
    return n;
}

template <typename T>
T as(const YAML::Node& parent, const std::string& key,
     const std::string& path) {
    auto n = require(parent, key, path);
    try {
        return n.as<T>();
    } catch (const YAML::Exception& e) {
        throw ConfigError("malformed config field " + path + "." + key + ": " + e.what());
    }
}

}

Config load_config(const std::filesystem::path& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::Exception& e) {
        throw ConfigError("cannot parse " + path.string() + ": " + e.what());
    }

    Config c;

    auto ssd = require(root, "ssd", "");
    c.ssd.raw_bandwidth_mbps       = as<int>(ssd, "raw_bandwidth_mbps", "ssd");
    c.ssd.effective_bandwidth_mbps = as<int>(ssd, "effective_bandwidth_mbps", "ssd");
    c.ssd.decompress_ratio         = as<double>(ssd, "decompress_ratio", "ssd");
    c.ssd.decompress_latency_us    = as<int>(ssd, "decompress_latency_us", "ssd");

    auto bus = require(root, "bus", "");
    c.bus.total_bandwidth_mbps     = as<int>(bus, "total_bandwidth_mbps", "bus");
    c.bus.transaction_overhead_us  = as<int>(bus, "transaction_overhead_us", "bus");

    auto npu = require(root, "npu", "");
    c.npu.cores           = as<int>(npu, "cores", "npu");
    c.npu.shared_cache_mb = as<int>(npu, "shared_cache_mb", "npu");

    auto audio = require(root, "audio", "");
    c.audio.rate_mbps    = as<int>(audio, "rate_mbps", "audio");
    c.audio.packet_bytes = as<int>(audio, "packet_bytes", "audio");
    c.audio.deadline_us  = as<int>(audio, "deadline_us", "audio");
    c.audio.priority     = as<std::string>(audio, "priority", "audio");

    auto tex = require(root, "texture", "");
    c.texture.burst_rate_mbps    = as<int>(tex, "burst_rate_mbps", "texture");
    c.texture.burst_duration_ms  = as<int>(tex, "burst_duration_ms", "texture");
    c.texture.block_bytes        = as<int>(tex, "block_bytes", "texture");
    c.texture.priority           = as<std::string>(tex, "priority", "texture");

    auto aw = require(root, "ai_weights", "");
    c.ai_weights.lod0_mb  = as<int>(aw, "lod0_mb", "ai_weights");
    c.ai_weights.lod1_mb  = as<int>(aw, "lod1_mb", "ai_weights");
    c.ai_weights.lod2_mb  = as<int>(aw, "lod2_mb", "ai_weights");
    c.ai_weights.priority = as<std::string>(aw, "priority", "ai_weights");

    auto lm = require(root, "lod_manager", "");
    c.lod_manager.tick_us        = as<int>(lm, "tick_us", "lod_manager");
    c.lod_manager.hysteresis_pct = as<int>(lm, "hysteresis_pct", "lod_manager");
    auto bands = require(lm, "bands", "lod_manager");
    if (!bands.IsSequence() || bands.size() == 0) {
        throw ConfigError("lod_manager.bands must be a non-empty sequence");
    }
    for (std::size_t i = 0; i < bands.size(); ++i) {
        LodBand b;
        std::string path = "lod_manager.bands[" + std::to_string(i) + "]";
        b.lod            = as<int>(bands[i], "lod", path);
        b.max_distance_m = as<int>(bands[i], "max_distance_m", path);
        c.lod_manager.bands.push_back(b);
    }

    auto pr = require(root, "predictor", "");
    c.predictor.policy = as<std::string>(pr, "policy", "predictor");

    auto sch = require(root, "scheduler", "");
    c.scheduler.policy                  = as<std::string>(sch, "policy", "scheduler");
    c.scheduler.quantum_us              = as<int>(sch, "quantum_us", "scheduler");
    c.scheduler.critical_rate_limit_pct = as<int>(sch, "critical_rate_limit_pct", "scheduler");
    auto bw = require(sch, "bulk_weights", "scheduler");
    c.scheduler.bulk_weights.high   = as<int>(bw, "high", "scheduler.bulk_weights");
    c.scheduler.bulk_weights.normal = as<int>(bw, "normal", "scheduler.bulk_weights");

    auto dma = require(root, "dma", "");
    c.dma.path = as<std::string>(dma, "path", "dma");

    auto ev = require(root, "eviction", "");
    c.eviction.policy = as<std::string>(ev, "policy", "eviction");

    auto deg = require(root, "degradation", "");
    c.degradation.weight_load_timeout_ms = as<int>(deg, "weight_load_timeout_ms", "degradation");

    return c;
}

}
