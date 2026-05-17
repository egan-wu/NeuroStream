#include "neurostream/latency_histogram.hpp"
#include <algorithm>
#include <cmath>

namespace neurostream {

LatencyHistogram::LatencyHistogram() {
    counts_.fill(0);
}

// Bucket i covers latency in [2^(i*0.5), 2^((i+1)*0.5)) µs, so each step
// is √2. Bucket 0 = [1, 1.41), bucket 49 ≈ [22.6 ms, 32 ms). This covers
// the audio P99 range we care about (1 µs → ~10 s).
std::size_t LatencyHistogram::bucket_for(Time us) {
    if (us <= 0) return 0;
    double log_us = std::log2(static_cast<double>(us));
    long b = static_cast<long>(std::floor(log_us * 2.0));
    if (b < 0) return 0;
    if (b >= static_cast<long>(kBucketCount)) return kBucketCount - 1;
    return static_cast<std::size_t>(b);
}

Time LatencyHistogram::upper_bound_of(std::size_t bucket) {
    if (bucket >= kBucketCount) bucket = kBucketCount - 1;
    double half_pow = static_cast<double>(bucket + 1) * 0.5;
    return static_cast<Time>(std::pow(2.0, half_pow));
}

void LatencyHistogram::record(Time latency_us) {
    if (latency_us < 0) latency_us = 0;
    auto b = bucket_for(latency_us);
    counts_[b]++;
    total_++;
    if (latency_us > max_) max_ = latency_us;
    sum_for_mean += static_cast<double>(latency_us);
}

void LatencyHistogram::merge(const LatencyHistogram& o) {
    for (std::size_t i = 0; i < kBucketCount; ++i) counts_[i] += o.counts_[i];
    total_ += o.total_;
    if (o.max_ > max_) max_ = o.max_;
    sum_for_mean += o.sum_for_mean;
}

Time LatencyHistogram::percentile(double p) const {
    if (total_ == 0) return 0;
    if (p < 0.0) p = 0.0;
    if (p > 1.0) p = 1.0;
    auto target = static_cast<std::uint64_t>(std::ceil(p * static_cast<double>(total_)));
    if (target == 0) target = 1;
    std::uint64_t cum = 0;
    for (std::size_t i = 0; i < kBucketCount; ++i) {
        cum += counts_[i];
        if (cum >= target) return upper_bound_of(i);
    }
    return max_;
}

double LatencyHistogram::mean_us() const noexcept {
    if (total_ == 0) return 0.0;
    return sum_for_mean / static_cast<double>(total_);
}

}
