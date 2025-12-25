#pragma once

#include <cstdint>
#include <vector>
#include <atomic>
#include <algorithm>

namespace perf {

// Latency statistics
struct LatencyStats {
    uint64_t min_ns;
    uint64_t max_ns;
    uint64_t mean_ns;
    uint64_t p50_ns;   // Median
    uint64_t p95_ns;
    uint64_t p99_ns;
    uint64_t p999_ns;
    uint64_t sample_count;
    
    LatencyStats() : min_ns(0), max_ns(0), mean_ns(0), 
                     p50_ns(0), p95_ns(0), p99_ns(0), p999_ns(0),
                     sample_count(0) {}
};

class LatencyTracker {
public:
    // bucket_size_ns: size of each histogram bucket in nanoseconds
    // max_latency_ns: maximum latency to track (samples above this go to last bucket)
    LatencyTracker(uint64_t bucket_size_ns = 100, 
                   uint64_t max_latency_ns = 1000000);  // Default: 100ns buckets, 1ms max
    
    ~LatencyTracker();
    
    // Record a latency sample (thread-safe)
    void record(uint64_t latency_ns);
    
    // Get current statistics (calculates percentiles from histogram)
    LatencyStats get_stats() const;
    
    // Reset all statistics
    void reset();
    
    // Export histogram to vector (for plotting/analysis)
    void export_histogram(std::vector<uint64_t>& buckets) const;
    
    // Configuration
    uint64_t get_bucket_size() const { return bucket_size_ns_; }
    size_t get_num_buckets() const { return histogram_.size(); }
    
private:
    uint64_t bucket_size_ns_;
    uint64_t max_latency_ns_;
    
    // Histogram: buckets[i] = count of samples in range [i*bucket_size, (i+1)*bucket_size)
    std::vector<std::atomic<uint64_t>> histogram_;
    
    // Min/max tracking
    std::atomic<uint64_t> min_latency_;
    std::atomic<uint64_t> max_latency_;
    
    // Total samples and sum (for mean calculation)
    std::atomic<uint64_t> total_samples_;
    std::atomic<uint64_t> total_latency_;
};

// Helper: High-resolution timestamp
inline uint64_t get_timestamp_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + 
           static_cast<uint64_t>(ts.tv_nsec);
}

// RAII timer for automatic latency measurement
class ScopedLatencyTimer {
public:
    ScopedLatencyTimer(LatencyTracker& tracker) 
        : tracker_(tracker), 
          start_time_(get_timestamp_ns()) {}
    
    ~ScopedLatencyTimer() {
        uint64_t end_time = get_timestamp_ns();
        uint64_t latency = end_time - start_time_;
        tracker_.record(latency);
    }
    
private:
    LatencyTracker& tracker_;
    uint64_t start_time_;
};

} // namespace perf