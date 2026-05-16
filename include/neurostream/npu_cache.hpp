#pragma once
#include "neurostream/time.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace neurostream {

// Key for a cached weight entry: (npc_id, lod) packed.
struct CacheKey {
    std::uint32_t npc_id;
    std::uint8_t  lod;

    bool operator==(const CacheKey& o) const noexcept {
        return npc_id == o.npc_id && lod == o.lod;
    }
};

struct CacheKeyHash {
    std::size_t operator()(const CacheKey& k) const noexcept {
        return (static_cast<std::size_t>(k.npc_id) << 8) |
               static_cast<std::size_t>(k.lod);
    }
};

// A single cache entry. `pin_count > 0` blocks eviction.
struct CacheEntry {
    CacheKey      key;
    std::uint32_t size_bytes;
    Time          last_access_us;
    double        last_distance_m;
    int           pin_count = 0;
};

// Abstract eviction policy: given the current resident set, pick the next
// victim to displace. Pinned entries (pin_count > 0) MUST be ignored. Returns
// nullopt if no evictable entry exists.
class EvictionPolicy {
public:
    virtual ~EvictionPolicy() = default;
    virtual std::optional<CacheKey>
    pick_victim(const std::vector<const CacheEntry*>& evictable) const = 0;
};

// Distance-descending; tie-break by oldest last_access_us. Mirrors the
// real-world heuristic "evict things the player is far from".
class DistanceLruPolicy : public EvictionPolicy {
public:
    std::optional<CacheKey>
    pick_victim(const std::vector<const CacheEntry*>& evictable) const override;
};

// Bounded NPU weight cache.
//
// Lifecycle:
//   1. admit(key, size, t, dist) — completion callback inserts a weight.
//      May evict zero or more existing entries to make room. Returns the
//      list of evicted keys for trace emission. If even after evicting all
//      evictable entries we still don't fit, the new entry is REJECTED
//      and the function returns AdmissionResult{accepted: false, ...}.
//   2. touch(key, t, dist) — keep last-access/distance fresh on cache hit.
//   3. pin(key) / unpin(key) — refcount-based eviction blocking.
//   4. is_resident(key) — query.
class NpuCache {
public:
    struct AdmissionResult {
        bool                  accepted = false;
        std::vector<CacheKey> evicted;
    };

    NpuCache(std::uint32_t capacity_bytes,
             std::unique_ptr<EvictionPolicy> policy);

    AdmissionResult admit(const CacheKey& key, std::uint32_t size_bytes,
                          Time now_us, double distance_m);

    void   touch(const CacheKey& key, Time now_us, double distance_m);
    bool   pin(const CacheKey& key);    // returns false if key not resident
    bool   unpin(const CacheKey& key);  // returns false if not pinned/not resident
    bool   is_resident(const CacheKey& key) const;
    bool   is_pinned(const CacheKey& key) const;

    std::uint32_t bytes_used() const noexcept { return bytes_used_; }
    std::uint32_t capacity()   const noexcept { return capacity_; }
    std::size_t   size()       const noexcept { return entries_.size(); }

    // Snapshot of resident keys (test helper).
    std::vector<CacheKey> resident_keys() const;

private:
    std::uint32_t                                                 capacity_;
    std::uint32_t                                                 bytes_used_ = 0;
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash>        entries_;
    std::unique_ptr<EvictionPolicy>                               policy_;
};

}
