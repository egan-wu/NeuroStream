#pragma once
#include "neurostream/time.hpp"
#include <array>
#include <cstdint>

namespace neurostream {

// Log-spaced histogram for audio packet latency. ~50 buckets covering
// 1 µs through 10 s. P99 / P99.9 / max are bucket-rounded.
//
// Memory: O(buckets), constant 50 × 8 byte = 400 byte. Replaces the
// per-sample vector storage from earlier phases (Phase 9 decision).
class LatencyHistogram {
public:
    static constexpr std::size_t kBucketCount = 50;

    LatencyHistogram();

    void   record(Time latency_us);
    void   merge(const LatencyHistogram& o);

    std::uint64_t count() const noexcept { return total_; }
    Time          max()   const noexcept { return max_; }

    // Percentile of the empirical distribution, rounded UP to the
    // bucket's upper bound. `p` is in [0, 1].
    Time percentile(double p) const;

    // Streaming mean, accumulated as samples land.
    double mean_us() const noexcept;

private:
    static std::size_t bucket_for(Time us);
    static Time        upper_bound_of(std::size_t bucket);

    std::array<std::uint64_t, kBucketCount> counts_;
    std::uint64_t total_      = 0;
    Time          max_        = 0;
    double        sum_for_mean = 0.0;
};

}
