#include "latency_tracker.h"
#include <iostream>
#include <random>
#include <thread>
#include <iomanip>

using namespace perf;

// Test 1: Basic recording
bool test_basic_recording() {
    std::cout << "\n=== Test 1: Basic Recording ===\n";
    
    LatencyTracker tracker(100, 10000);  // 100ns buckets, 10us max
    
    // Record some samples
    tracker.record(150);   // 150ns
    tracker.record(250);   // 250ns
    tracker.record(350);   // 350ns
    tracker.record(450);   // 450ns
    tracker.record(550);   // 550ns
    
    LatencyStats stats = tracker.get_stats();
    
    std::cout << "Recorded 5 samples\n";
    std::cout << "Min: " << stats.min_ns << "ns\n";
    std::cout << "Max: " << stats.max_ns << "ns\n";
    std::cout << "Mean: " << stats.mean_ns << "ns\n";
    std::cout << "p50: " << stats.p50_ns << "ns\n";
    
    if (stats.sample_count != 5) {
        std::cerr << "FAIL: Expected 5 samples\n";
        return false;
    }
    
    if (stats.min_ns != 150 || stats.max_ns != 550) {
        std::cerr << "FAIL: Min/max incorrect\n";
        return false;
    }
    
    std::cout << "PASS\n";
    return true;
}

// Test 2: Percentile calculation
bool test_percentiles() {
    std::cout << "\n=== Test 2: Percentile Calculation ===\n";
    
    LatencyTracker tracker(10, 10000);  // 10ns buckets
    
    // Record 1000 samples with known distribution
    for (int i = 1; i <= 1000; ++i) {
        tracker.record(i);  // 1ns, 2ns, 3ns, ..., 1000ns
    }
    
    LatencyStats stats = tracker.get_stats();
    
    std::cout << "Recorded 1000 samples (1-1000ns)\n";
    std::cout << "p50: " << stats.p50_ns << "ns (expected ~500)\n";
    std::cout << "p95: " << stats.p95_ns << "ns (expected ~950)\n";
    std::cout << "p99: " << stats.p99_ns << "ns (expected ~990)\n";
    
    // Allow some tolerance due to bucketing
    if (stats.p50_ns < 490 || stats.p50_ns > 510) {
        std::cerr << "FAIL: p50 out of range\n";
        return false;
    }
    
    if (stats.p95_ns < 940 || stats.p95_ns > 960) {
        std::cerr << "FAIL: p95 out of range\n";
        return false;
    }
    
    if (stats.p99_ns < 980 || stats.p99_ns > 1000) {
        std::cerr << "FAIL: p99 out of range\n";
        return false;
    }
    
    std::cout << "PASS\n";
    return true;
}

// Test 3: Concurrent recording
bool test_concurrent_recording() {
    std::cout << "\n=== Test 3: Concurrent Recording ===\n";
    
    LatencyTracker tracker(100, 100000);  // 100ns buckets, 100us max
    
    const int NUM_THREADS = 4;
    const int SAMPLES_PER_THREAD = 10000;
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            std::mt19937 rng(t);
            std::uniform_int_distribution<uint64_t> dist(100, 10000);
            
            for (int i = 0; i < SAMPLES_PER_THREAD; ++i) {
                tracker.record(dist(rng));
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    LatencyStats stats = tracker.get_stats();
    
    std::cout << "Recorded " << stats.sample_count << " samples from " 
              << NUM_THREADS << " threads\n";
    std::cout << "Min: " << stats.min_ns << "ns\n";
    std::cout << "Max: " << stats.max_ns << "ns\n";
    std::cout << "Mean: " << stats.mean_ns << "ns\n";
    
    uint64_t expected_samples = NUM_THREADS * SAMPLES_PER_THREAD;
    if (stats.sample_count != expected_samples) {
        std::cerr << "FAIL: Expected " << expected_samples 
                  << " samples, got " << stats.sample_count << "\n";
        return false;
    }
    
    std::cout << "PASS\n";
    return true;
}

// Test 4: Reset functionality
bool test_reset() {
    std::cout << "\n=== Test 4: Reset Functionality ===\n";
    
    LatencyTracker tracker(100, 10000);
    
    // Record some samples
    for (int i = 0; i < 100; ++i) {
        tracker.record(500 + i);
    }
    
    LatencyStats stats1 = tracker.get_stats();
    std::cout << "Before reset: " << stats1.sample_count << " samples\n";
    
    // Reset
    tracker.reset();
    
    LatencyStats stats2 = tracker.get_stats();
    std::cout << "After reset: " << stats2.sample_count << " samples\n";
    
    if (stats2.sample_count != 0) {
        std::cerr << "FAIL: Reset didn't clear samples\n";
        return false;
    }
    
    std::cout << "PASS\n";
    return true;
}

// Test 5: Recording overhead
bool test_recording_overhead() {
    std::cout << "\n=== Test 5: Recording Overhead ===\n";
    
    LatencyTracker tracker(10, 1000000);
    
    const int NUM_SAMPLES = 1000000;
    
    auto start = get_timestamp_ns();
    
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        tracker.record(100 + (i % 1000));
    }
    
    auto end = get_timestamp_ns();
    
    uint64_t total_time_ns = end - start;
    double avg_overhead_ns = static_cast<double>(total_time_ns) / NUM_SAMPLES;
    
    std::cout << "Recorded " << NUM_SAMPLES << " samples in " 
              << (total_time_ns / 1000000) << "ms\n";
    std::cout << "Average recording overhead: " << std::fixed 
              << std::setprecision(1) << avg_overhead_ns << "ns\n";
    
    if (avg_overhead_ns > 50) {
        std::cerr << "WARNING: Recording overhead > 50ns\n";
    }
    
    std::cout << "PASS\n";
    return true;
}

// Test 6: Scoped timer
bool test_scoped_timer() {
    std::cout << "\n=== Test 6: Scoped Timer ===\n";
    
    LatencyTracker tracker(1000, 1000000);
    
    // Simulate work with scoped timer
    for (int i = 0; i < 10; ++i) {
        ScopedLatencyTimer timer(tracker);
        
        // Simulate some work (busy wait for ~10us)
        auto start = get_timestamp_ns();
        while ((get_timestamp_ns() - start) < 10000) {
            // Busy wait
        }
    }
    
    LatencyStats stats = tracker.get_stats();
    
    std::cout << "Measured 10 operations with scoped timer\n";
    std::cout << "Mean latency: " << stats.mean_ns << "ns (~10000ns expected)\n";
    
    if (stats.sample_count != 10) {
        std::cerr << "FAIL: Expected 10 samples\n";
        return false;
    }
    
    // Should be roughly 10us per operation
    if (stats.mean_ns < 9000 || stats.mean_ns > 11000) {
        std::cerr << "WARNING: Mean latency outside expected range\n";
    }
    
    std::cout << "PASS\n";
    return true;
}

int main() {
    std::cout << "========================================\n";
    std::cout << "     Latency Tracker Tests\n";
    std::cout << "========================================\n";
    
    int passed = 0;
    int total = 6;
    
    if (test_basic_recording()) passed++;
    if (test_percentiles()) passed++;
    if (test_concurrent_recording()) passed++;
    if (test_reset()) passed++;
    if (test_recording_overhead()) passed++;
    if (test_scoped_timer()) passed++;
    
    std::cout << "\n========================================\n";
    std::cout << "Results: " << passed << "/" << total << " tests passed\n";
    std::cout << "========================================\n";
    
    return (passed == total) ? 0 : 1;
}