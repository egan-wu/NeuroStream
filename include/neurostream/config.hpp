#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

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

struct LodBand {
    int lod;
    int max_distance_m;
};

struct LodManagerConfig {
    int                  tick_us;
    int                  hysteresis_pct;     // deadband width as % of band max
    std::vector<LodBand> bands;
};

struct PredictorConfig {
    std::string policy;                      // "scripted" | "lod" | "intent"
};

struct IntentPredictorConfig {
    int    fov_deg;
    int    look_ahead_ms;
    int    close_m;
    int    near_m;
    int    visible_m;
    double stopped_dist_m;
    double stopped_speed_m_s;
};

struct BulkWeights {
    int high;
    int normal;
};

struct SchedulerConfig {
    std::string  policy;                   // "fifo" | "qos"
    int          quantum_us;               // re-arbitration granularity
    int          critical_rate_limit_pct;  // 0–100, % of bus bandwidth
    BulkWeights  bulk_weights;
};

struct BounceDmaConfig {
    int memcpy_bandwidth_mbps;
    int cycles_per_byte;
};

struct NeuroDmaConfig {
    int sgl_entry_bytes;
    int setup_cost_cycles;
};

struct DmaConfig {
    std::string     path;          // "bounce" | "neuro_dma"
    BounceDmaConfig bounce;
    NeuroDmaConfig  neuro_dma;
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
    AiWeightsConfig       ai_weights;
    LodManagerConfig      lod_manager;
    PredictorConfig       predictor;
    IntentPredictorConfig intent_predictor;
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
