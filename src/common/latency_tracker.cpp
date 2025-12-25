#include "latency_tracker.h"
#include <iostream>
#include <limits>
#include <ctime>

namespace perf {

LatencyTracker::LatencyTracker(uint64_t bucket_size_ns, uint64_t max_latency_ns)
    : bucket_size_ns_(bucket_size_ns),
      max_latency_ns_(max_latency_ns),
      min_latency_(std::numeric_limits<uint64_t>::max()),
      max_latency_(0),
      total_samples_(0),
      total_latency_(0) {
    
    // Calculate number of buckets needed
    size_t num_buckets = (max_latency_ns / bucket_size_ns) + 1;
    histogram_.resize(num_buckets);
    
    // Initialize all buckets to 0
    for (auto& bucket : histogram_) {
        bucket.store(0, std::memory_order_relaxed);
    }
}

LatencyTracker::~LatencyTracker() = default;

void LatencyTracker::record(uint64_t latency_ns) {
    // Update min/max
    uint64_t current_min = min_latency_.load(std::memory_order_relaxed);
    while (latency_ns < current_min) {
        if (min_latency_.compare_exchange_weak(current_min, latency_ns, 
                                                std::memory_order_relaxed)) {
            break;
        }
    }
    
    uint64_t current_max = max_latency_.load(std::memory_order_relaxed);
    while (latency_ns > current_max) {
        if (max_latency_.compare_exchange_weak(current_max, latency_ns,
                                                std::memory_order_relaxed)) {
            break;
        }
    }
    
    // Update histogram
    size_t bucket_index = latency_ns / bucket_size_ns_;
    if (bucket_index >= histogram_.size()) {
        bucket_index = histogram_.size() - 1;  // Overflow bucket
    }
    
    histogram_[bucket_index].fetch_add(1, std::memory_order_relaxed);
    
    // Update totals
    total_samples_.fetch_add(1, std::memory_order_relaxed);
    total_latency_.fetch_add(latency_ns, std::memory_order_relaxed);
}

LatencyStats LatencyTracker::get_stats() const {
    LatencyStats stats;
    
    uint64_t sample_count = total_samples_.load(std::memory_order_relaxed);
    if (sample_count == 0) {
        return stats;  // No samples yet
    }
    
    stats.sample_count = sample_count;
    stats.min_ns = min_latency_.load(std::memory_order_relaxed);
    stats.max_ns = max_latency_.load(std::memory_order_relaxed);
    
    // Calculate mean
    uint64_t total_latency = total_latency_.load(std::memory_order_relaxed);
    stats.mean_ns = total_latency / sample_count;
    
    // Calculate percentiles from histogram
    // We need to find bucket indices where cumulative count reaches percentile thresholds
    
    uint64_t p50_target = sample_count * 50 / 100;
    uint64_t p95_target = sample_count * 95 / 100;
    uint64_t p99_target = sample_count * 99 / 100;
    uint64_t p999_target = sample_count * 999 / 1000;
    
    uint64_t cumulative = 0;
    bool found_p50 = false, found_p95 = false, found_p99 = false, found_p999 = false;
    
    for (size_t i = 0; i < histogram_.size(); ++i) {
        uint64_t count = histogram_[i].load(std::memory_order_relaxed);
        cumulative += count;
        
        if (!found_p50 && cumulative >= p50_target) {
            stats.p50_ns = i * bucket_size_ns_;
            found_p50 = true;
        }
        if (!found_p95 && cumulative >= p95_target) {
            stats.p95_ns = i * bucket_size_ns_;
            found_p95 = true;
        }
        if (!found_p99 && cumulative >= p99_target) {
            stats.p99_ns = i * bucket_size_ns_;
            found_p99 = true;
        }
        if (!found_p999 && cumulative >= p999_target) {
            stats.p999_ns = i * bucket_size_ns_;
            found_p999 = true;
            break;  // Found all percentiles
        }
    }
    
    return stats;
}

void LatencyTracker::reset() {
    // Reset all atomics
    for (auto& bucket : histogram_) {
        bucket.store(0, std::memory_order_relaxed);
    }
    
    min_latency_.store(std::numeric_limits<uint64_t>::max(), std::memory_order_relaxed);
    max_latency_.store(0, std::memory_order_relaxed);
    total_samples_.store(0, std::memory_order_relaxed);
    total_latency_.store(0, std::memory_order_relaxed);
}

void LatencyTracker::export_histogram(std::vector<uint64_t>& buckets) const {
    buckets.clear();
    buckets.reserve(histogram_.size());
    
    for (const auto& bucket : histogram_) {
        buckets.push_back(bucket.load(std::memory_order_relaxed));
    }
}

} // namespace perf