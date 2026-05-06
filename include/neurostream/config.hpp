#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>

namespace neurostream {

struct SsdConfig {
    int    raw_bandwidth_mbps;
    int    effective_bandwidth_mbps;
    double decompress_ratio;
    int    decompress_latency_us;
};

struct BusConfig {
    int total_bandwidth_mbps;
    int transaction_overhead_us;
};

struct NpuConfig {
    int cores;
    int shared_cache_mb;
};

struct AudioConfig {
    int         rate_mbps;
    int         packet_bytes;
    int         deadline_us;
    std::string priority;
};

struct TextureConfig {
    int         burst_rate_mbps;
    int         burst_duration_ms;
    int         block_bytes;
    std::string priority;
};

struct AiWeightsConfig {
    int         lod0_mb;
    int         lod1_mb;
    int         lod2_mb;
    std::string priority;
};

struct QosWeights {
    int critical;
    int high;
    int normal;
};

struct SchedulerConfig {
    std::string policy;        // "fifo" | "qos"
    QosWeights  qos_weights;
};

struct DmaConfig {
    std::string path;          // "bounce" | "p2p"
};

struct EvictionConfig {
    std::string policy;        // "distance_lru"
};

struct DegradationConfig {
    int weight_load_timeout_ms;
};

struct Config {
    SsdConfig         ssd;
    BusConfig         bus;
    NpuConfig         npu;
    AudioConfig       audio;
    TextureConfig     texture;
    AiWeightsConfig   ai_weights;
    SchedulerConfig   scheduler;
    DmaConfig         dma;
    EvictionConfig    eviction;
    DegradationConfig degradation;
};

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Config load_config(const std::filesystem::path& path);

}
