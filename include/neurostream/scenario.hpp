#pragma once
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace neurostream {

struct TextureBurstSpec {
    int at_ms;
    int rate_mbps;
    int duration_ms;
};

struct Waypoint {
    int    at_ms;
    double distance_m;
    // Reserved for Phase 6: 2D position. Phase 4 ignores these.
    double pos_x = 0.0;
    double pos_y = 0.0;
};

struct NpcSpec {
    std::uint32_t         id;
    std::vector<Waypoint> waypoints;        // sorted by at_ms
    // Reserved for Phase 6: quest-NPC override etc.
    std::string priority = "normal";
};

struct Scenario {
    std::string                   name;
    int                           duration_ms = 0;
    bool                          audio_enabled = false;
    std::vector<TextureBurstSpec> texture_bursts;
    std::vector<NpcSpec>          npcs;
};

class ScenarioError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Scenario load_scenario(const std::filesystem::path& path);

// Linear interpolation of distance from a sorted waypoint list.
// Before first waypoint: returns first waypoint's distance.
// After last:            returns last waypoint's distance.
double distance_at(const std::vector<Waypoint>& waypoints, int t_ms);

}
