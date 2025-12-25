#pragma once

#include <atomic>
#include <cstdint>
#include <vector>
#include <cstring>

namespace cache {

// Market state for a symbol
struct MarketState {
    double best_bid;
    double best_ask;
    uint32_t bid_quantity;
    uint32_t ask_quantity;
    double last_traded_price;
    uint32_t last_traded_quantity;
    uint64_t last_update_time;
    uint64_t update_count;
    
    MarketState() 
        : best_bid(0.0),
          best_ask(0.0),
          bid_quantity(0),
          ask_quantity(0),
          last_traded_price(0.0),
          last_traded_quantity(0),
          last_update_time(0),
          update_count(0) {}
} __attribute__((aligned(64)));  // Cache line alignment to prevent false sharing

// Atomic wrapper for MarketState
// Uses sequence lock pattern for lock-free consistent reads
struct AtomicMarketState {
    alignas(64) std::atomic<uint64_t> sequence;  // Sequence counter for optimistic reads
    MarketState data;
    
    AtomicMarketState() : sequence(0), data() {}
};

class SymbolCache {
public:
    SymbolCache(size_t num_symbols);
    ~SymbolCache();
    
    // Writer operations (single writer thread)
    void update_bid(uint16_t symbol_id, double price, uint32_t quantity);
    void update_ask(uint16_t symbol_id, double price, uint32_t quantity);
    void update_trade(uint16_t symbol_id, double price, uint32_t quantity);
    void update_quote(uint16_t symbol_id, double bid_price, uint32_t bid_qty,
                      double ask_price, uint32_t ask_qty);
    
    // Reader operations (lock-free, can be called from any thread)
    MarketState get_snapshot(uint16_t symbol_id) const;
    
    // Batch read for multiple symbols
    void get_snapshots(const std::vector<uint16_t>& symbol_ids,
                       std::vector<MarketState>& out_states) const;
    
    // Statistics
    size_t get_num_symbols() const { return num_symbols_; }
    uint64_t get_total_updates() const;
    
private:
    size_t num_symbols_;
    std::vector<AtomicMarketState> states_;
};

} // namespace cache