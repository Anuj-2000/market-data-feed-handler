#include "cache.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>

using namespace cache;

// Test 1: Basic read/write
bool test_basic_operations() {
    std::cout << "\n=== Test 1: Basic Operations ===\n";
    
    SymbolCache cache(10);
    
    // Update quote for symbol 0
    cache.update_quote(0, 1234.50, 1000, 1235.50, 1500);
    
    // Read back
    MarketState state = cache.get_snapshot(0);
    
    std::cout << "Symbol 0 state:\n";
    std::cout << "  Bid: " << state.best_bid << " x " << state.bid_quantity << "\n";
    std::cout << "  Ask: " << state.best_ask << " x " << state.ask_quantity << "\n";
    std::cout << "  Updates: " << state.update_count << "\n";
    
    if (state.best_bid != 1234.50 || state.best_ask != 1235.50) {
        std::cerr << "FAIL: Quote values incorrect\n";
        return false;
    }
    
    // Update trade
    cache.update_trade(0, 1235.00, 500);
    state = cache.get_snapshot(0);
    
    std::cout << "  Last Trade: " << state.last_traded_price 
              << " x " << state.last_traded_quantity << "\n";
    
    if (state.last_traded_price != 1235.00) {
        std::cerr << "FAIL: Trade value incorrect\n";
        return false;
    }
    
    std::cout << "PASS: Basic operations working\n";
    return true;
}

// Test 2: Multi-symbol updates
bool test_multi_symbol() {
    std::cout << "\n=== Test 2: Multi-Symbol Updates ===\n";
    
    SymbolCache cache(100);
    
    // Update 50 symbols
    for (uint16_t i = 0; i < 50; ++i) {
        double base_price = 1000.0 + i * 10.0;
        cache.update_quote(i, base_price, 1000, base_price + 1.0, 1500);
    }
    
    // Verify all symbols
    for (uint16_t i = 0; i < 50; ++i) {
        MarketState state = cache.get_snapshot(i);
        double expected_bid = 1000.0 + i * 10.0;
        
        if (std::abs(state.best_bid - expected_bid) > 0.01) {
            std::cerr << "FAIL: Symbol " << i << " has incorrect bid\n";
            return false;
        }
    }
    
    std::cout << "All 50 symbols updated and verified correctly\n";
    std::cout << "PASS: Multi-symbol operations working\n";
    return true;
}

// Test 3: Concurrent reads (single writer, multiple readers)
bool test_concurrent_reads() {
    std::cout << "\n=== Test 3: Concurrent Reads (Lock-Free) ===\n";
    
    SymbolCache cache(10);
    
    // Initialize symbol 0
    cache.update_quote(0, 1000.0, 1000, 1001.0, 1500);
    
    std::atomic<bool> writer_running{true};
    std::atomic<uint64_t> reader_reads{0};
    std::atomic<uint64_t> inconsistent_reads{0};
    
    // Writer thread: continuously updates symbol 0
    std::thread writer([&]() {
        for (int i = 0; i < 10000; ++i) {
            double price = 1000.0 + i;
            cache.update_quote(0, price, 1000, price + 1.0, 1500);
        }
        writer_running = false;
    });
    
    // Reader threads: continuously read symbol 0
    std::vector<std::thread> readers;
    for (int t = 0; t < 4; ++t) {
        readers.emplace_back([&]() {
            while (writer_running) {
                MarketState state = cache.get_snapshot(0);
                reader_reads++;
                
                // Verify consistency: ask should always be bid + 1.0
                if (std::abs((state.best_ask - state.best_bid) - 1.0) > 0.01) {
                    inconsistent_reads++;
                }
            }
        });
    }
    
    writer.join();
    for (auto& reader : readers) {
        reader.join();
    }
    
    std::cout << "Total reads: " << reader_reads << "\n";
    std::cout << "Inconsistent reads: " << inconsistent_reads << "\n";
    
    if (inconsistent_reads > 0) {
        std::cerr << "FAIL: Detected inconsistent reads\n";
        return false;
    }
    
    std::cout << "PASS: All reads were consistent (lock-free seqlock working)\n";
    return true;
}

// Test 4: Read performance
bool test_read_performance() {
    std::cout << "\n=== Test 4: Read Performance ===\n";
    
    SymbolCache cache(100);
    
    // Initialize all symbols
    for (uint16_t i = 0; i < 100; ++i) {
        cache.update_quote(i, 1000.0 + i, 1000, 1001.0 + i, 1500);
    }
    
    // Benchmark reads
    const int NUM_READS = 1000000;
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < NUM_READS; ++i) {
        uint16_t symbol = i % 100;
        MarketState state = cache.get_snapshot(symbol);
        (void)state;  // Prevent optimization
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    
    double avg_latency_ns = duration.count() / static_cast<double>(NUM_READS);
    
    std::cout << "Read " << NUM_READS << " snapshots in " 
              << (duration.count() / 1000000) << " ms\n";
    std::cout << "Average read latency: " << std::fixed << std::setprecision(1) 
              << avg_latency_ns << " ns\n";
    
    if (avg_latency_ns > 100) {
        std::cerr << "WARNING: Read latency > 100ns (target is <50ns)\n";
    }
    
    std::cout << "PASS: Read performance measured\n";
    return true;
}

// Test 5: Update count tracking
bool test_update_tracking() {
    std::cout << "\n=== Test 5: Update Count Tracking ===\n";
    
    SymbolCache cache(5);
    
    // Update symbol 0 multiple times
    cache.update_quote(0, 1000.0, 1000, 1001.0, 1500);
    cache.update_trade(0, 1000.5, 500);
    cache.update_bid(0, 999.5, 1200);
    cache.update_ask(0, 1001.5, 1600);
    
    MarketState state = cache.get_snapshot(0);
    
    std::cout << "Symbol 0 update count: " << state.update_count << "\n";
    
    if (state.update_count != 4) {
        std::cerr << "FAIL: Expected 4 updates, got " << state.update_count << "\n";
        return false;
    }
    
    std::cout << "PASS: Update count tracked correctly\n";
    return true;
}

// Test 6: Batch read
bool test_batch_read() {
    std::cout << "\n=== Test 6: Batch Read ===\n";
    
    SymbolCache cache(100);
    
    // Initialize symbols
    for (uint16_t i = 0; i < 100; ++i) {
        cache.update_quote(i, 1000.0 + i, 1000, 1001.0 + i, 1500);
    }
    
    // Batch read symbols 10-19
    std::vector<uint16_t> symbol_ids = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    std::vector<MarketState> states;
    
    cache.get_snapshots(symbol_ids, states);
    
    if (states.size() != 10) {
        std::cerr << "FAIL: Expected 10 states, got " << states.size() << "\n";
        return false;
    }
    
    // Verify first and last
    if (std::abs(states[0].best_bid - 1010.0) > 0.01 ||
        std::abs(states[9].best_bid - 1019.0) > 0.01) {
        std::cerr << "FAIL: Batch read values incorrect\n";
        return false;
    }
    
    std::cout << "Read " << states.size() << " symbols in batch\n";
    std::cout << "PASS: Batch read working\n";
    return true;
}

int main() {
    std::cout << "========================================\n";
    std::cout << "     Lock-Free Symbol Cache Tests\n";
    std::cout << "========================================\n";
    
    int passed = 0;
    int total = 6;
    
    if (test_basic_operations()) passed++;
    if (test_multi_symbol()) passed++;
    if (test_concurrent_reads()) passed++;
    if (test_read_performance()) passed++;
    if (test_update_tracking()) passed++;
    if (test_batch_read()) passed++;
    
    std::cout << "\n========================================\n";
    std::cout << "Results: " << passed << "/" << total << " tests passed\n";
    std::cout << "========================================\n";
    
    return (passed == total) ? 0 : 1;
}