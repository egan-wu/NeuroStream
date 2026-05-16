#include "neurostream/npu_cache.hpp"
#include <algorithm>

namespace neurostream {

std::optional<CacheKey>
DistanceLruPolicy::pick_victim(const std::vector<const CacheEntry*>& evictable) const {
    if (evictable.empty()) return std::nullopt;
    // Primary: largest distance_m. Tie-break: smallest last_access_us
    // (oldest). This makes "far AND stale" the first to go.
    const CacheEntry* worst = evictable.front();
    for (const auto* e : evictable) {
        if (e->last_distance_m > worst->last_distance_m) {
            worst = e;
        } else if (e->last_distance_m == worst->last_distance_m &&
                   e->last_access_us < worst->last_access_us) {
            worst = e;
        }
    }
    return worst->key;
}

NpuCache::NpuCache(std::uint32_t capacity_bytes,
                   std::unique_ptr<EvictionPolicy> policy)
    : capacity_(capacity_bytes), policy_(std::move(policy)) {}

NpuCache::AdmissionResult
NpuCache::admit(const CacheKey& key, std::uint32_t size_bytes,
                Time now_us, double distance_m) {
    AdmissionResult res;

    // Already resident? Just refresh, no eviction, no admission.
    if (auto it = entries_.find(key); it != entries_.end()) {
        it->second.last_access_us = now_us;
        it->second.last_distance_m = distance_m;
        res.accepted = true;
        return res;
    }

    if (size_bytes > capacity_) {
        // Single entry larger than the entire cache — impossible to fit.
        res.accepted = false;
        return res;
    }

    // Evict until we have room.
    while (bytes_used_ + size_bytes > capacity_) {
        std::vector<const CacheEntry*> evictable;
        evictable.reserve(entries_.size());
        for (const auto& [k, e] : entries_) {
            if (e.pin_count == 0) evictable.push_back(&e);
        }
        auto victim = policy_->pick_victim(evictable);
        if (!victim) {
            // No evictable entry — admission refused.
            res.accepted = false;
            return res;
        }
        auto v_it = entries_.find(*victim);
        bytes_used_ -= v_it->second.size_bytes;
        entries_.erase(v_it);
        res.evicted.push_back(*victim);
    }

    CacheEntry e;
    e.key            = key;
    e.size_bytes     = size_bytes;
    e.last_access_us = now_us;
    e.last_distance_m = distance_m;
    e.pin_count      = 0;
    entries_.emplace(key, e);
    bytes_used_     += size_bytes;
    res.accepted     = true;
    return res;
}

void NpuCache::touch(const CacheKey& key, Time now_us, double distance_m) {
    if (auto it = entries_.find(key); it != entries_.end()) {
        it->second.last_access_us = now_us;
        it->second.last_distance_m = distance_m;
    }
}

bool NpuCache::pin(const CacheKey& key) {
    auto it = entries_.find(key);
    if (it == entries_.end()) return false;
    ++it->second.pin_count;
    return true;
}

bool NpuCache::unpin(const CacheKey& key) {
    auto it = entries_.find(key);
    if (it == entries_.end() || it->second.pin_count == 0) return false;
    --it->second.pin_count;
    return true;
}

bool NpuCache::is_resident(const CacheKey& key) const {
    return entries_.count(key) > 0;
}

bool NpuCache::is_pinned(const CacheKey& key) const {
    auto it = entries_.find(key);
    return it != entries_.end() && it->second.pin_count > 0;
}

std::vector<CacheKey> NpuCache::resident_keys() const {
    std::vector<CacheKey> ks;
    ks.reserve(entries_.size());
    for (const auto& [k, _] : entries_) ks.push_back(k);
    return ks;
}

}
