#pragma once
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace neurostream {

inline constexpr int kScenarioSchemaVersion = 3;

struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

inline double distance(const Vec2& a, const Vec2& b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

struct TextureBurstSpec {
    int at_ms;
    int rate_mbps;
    int duration_ms;
};

// Phase 6 v3 — 2D waypoint. `at_ms` and `pos` are required; `facing_deg` is
// only meaningful for the player.
struct Waypoint {
    int                  at_ms;
    Vec2                 pos;
    std::optional<double> facing_deg;
};

struct PlayerSpec {
    std::vector<Waypoint> waypoints;     // ≥1, sorted by at_ms
};

struct NpcSpec {
    std::uint32_t         id;
    std::vector<Waypoint> waypoints;     // ≥1, sorted by at_ms
    std::string           priority = "normal";    // "normal" | "quest"
};

struct InteractionSpec {
    int           at_ms;
    std::uint32_t npc_id;
    int           duration_ms;
};

struct Scenario {
    int                            schema_version = 0;
    std::string                    name;
    int                            duration_ms = 0;
    bool                           audio_enabled = false;
    std::optional<int>             intent_fov_deg_override;  // scenario-local override
    std::optional<int>             intent_look_ahead_ms_override;
    PlayerSpec                     player;
    std::vector<TextureBurstSpec>  texture_bursts;
    std::vector<NpcSpec>           npcs;
    std::vector<InteractionSpec>   interactions;
};

class ScenarioError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Scenario load_scenario(const std::filesystem::path& path);

// Linear interpolation: position at time t_ms from a sorted waypoint list.
// Before first / after last waypoint, returns the endpoint position.
Vec2 position_at(const std::vector<Waypoint>& waypoints, int t_ms);

// Approximate velocity (m/ms) from waypoint slope around t_ms.
// Single-waypoint lists return {0, 0}.
Vec2 velocity_at(const std::vector<Waypoint>& waypoints, int t_ms);

// Facing at time t_ms — linearly interpolated from waypoints that carry
// `facing_deg`. Waypoints without facing inherit the most recent specified
// value. Returns 0.0 if no waypoint specifies facing.
double facing_at(const std::vector<Waypoint>& waypoints, int t_ms);

}
