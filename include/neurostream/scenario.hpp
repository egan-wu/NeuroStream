#pragma once
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace neurostream {

struct TextureBurstSpec {
    int at_ms;
    int rate_mbps;
    int duration_ms;
};

struct WeightPrefetchSpec {
    int           at_ms;
    std::uint32_t npc_id;
    int           lod;
};

struct Scenario {
    std::string                     name;
    int                             duration_ms = 0;
    bool                            audio_enabled = false;
    std::vector<TextureBurstSpec>   texture_bursts;
    std::vector<WeightPrefetchSpec> weight_prefetches;
};

class ScenarioError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Scenario load_scenario(const std::filesystem::path& path);

}
