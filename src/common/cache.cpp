#include "cache.h"
#include "protocol.h"
#include <iostream>

namespace cache {

SymbolCache::SymbolCache(size_t num_symbols) 
    : num_symbols_(num_symbols),
      states_(num_symbols) {
    
    std::cout << "Initialized symbol cache for " << num_symbols << " symbols\n";
    std::cout << "Cache line size: 64 bytes\n";
    std::cout << "MarketState size: " << sizeof(MarketState) << " bytes\n";
    std::cout << "Total cache size: " 
              << (sizeof(AtomicMarketState) * num_symbols / 1024) << " KB\n";
}

SymbolCache::~SymbolCache() = default;

void SymbolCache::update_bid(uint16_t symbol_id, double price, uint32_t quantity) {
    if (symbol_id >= num_symbols_) return;
    
    auto& state = states_[symbol_id];
    
    // Increment sequence (odd = write in progress)
    uint64_t seq = state.sequence.load(std::memory_order_relaxed);
    state.sequence.store(seq + 1, std::memory_order_release);
    
    // Update data
    state.data.best_bid = price;
    state.data.bid_quantity = quantity;
    state.data.last_update_time = protocol::get_timestamp_ns();
    state.data.update_count++;
    
    // Increment sequence again (even = write complete)
    state.sequence.store(seq + 2, std::memory_order_release);
}

void SymbolCache::update_ask(uint16_t symbol_id, double price, uint32_t quantity) {
    if (symbol_id >= num_symbols_) return;
    
    auto& state = states_[symbol_id];
    
    uint64_t seq = state.sequence.load(std::memory_order_relaxed);
    state.sequence.store(seq + 1, std::memory_order_release);
    
    state.data.best_ask = price;
    state.data.ask_quantity = quantity;
    state.data.last_update_time = protocol::get_timestamp_ns();
    state.data.update_count++;
    
    state.sequence.store(seq + 2, std::memory_order_release);
}

void SymbolCache::update_trade(uint16_t symbol_id, double price, uint32_t quantity) {
    if (symbol_id >= num_symbols_) return;
    
    auto& state = states_[symbol_id];
    
    uint64_t seq = state.sequence.load(std::memory_order_relaxed);
    state.sequence.store(seq + 1, std::memory_order_release);
    
    state.data.last_traded_price = price;
    state.data.last_traded_quantity = quantity;
    state.data.last_update_time = protocol::get_timestamp_ns();
    state.data.update_count++;
    
    state.sequence.store(seq + 2, std::memory_order_release);
}

void SymbolCache::update_quote(uint16_t symbol_id, 
                                double bid_price, uint32_t bid_qty,
                                double ask_price, uint32_t ask_qty) {
    if (symbol_id >= num_symbols_) return;
    
    auto& state = states_[symbol_id];
    
    uint64_t seq = state.sequence.load(std::memory_order_relaxed);
    state.sequence.store(seq + 1, std::memory_order_release);
    
    // Update all quote fields atomically (from reader's perspective)
    state.data.best_bid = bid_price;
    state.data.bid_quantity = bid_qty;
    state.data.best_ask = ask_price;
    state.data.ask_quantity = ask_qty;
    state.data.last_update_time = protocol::get_timestamp_ns();
    state.data.update_count++;
    
    state.sequence.store(seq + 2, std::memory_order_release);
}

MarketState SymbolCache::get_snapshot(uint16_t symbol_id) const {
    if (symbol_id >= num_symbols_) {
        return MarketState();
    }
    
    const auto& state = states_[symbol_id];
    MarketState snapshot;
    
    // Optimistic lock-free read using sequence number
    // This is the "seqlock" pattern used in Linux kernel
    while (true) {
        // Read sequence number (even = no write in progress)
        uint64_t seq1 = state.sequence.load(std::memory_order_acquire);
        
        // If odd, writer is active - retry
        if (seq1 & 1) {
            continue;
        }
        
        // Read data
        snapshot = state.data;
        
        // Check if sequence changed during read
        uint64_t seq2 = state.sequence.load(std::memory_order_acquire);
        
        if (seq1 == seq2) {
            // Consistent read - success!
            return snapshot;
        }
        
        // Sequence changed, writer was active - retry
    }
}

void SymbolCache::get_snapshots(const std::vector<uint16_t>& symbol_ids,
                                std::vector<MarketState>& out_states) const {
    out_states.clear();
    out_states.reserve(symbol_ids.size());
    
    for (uint16_t symbol_id : symbol_ids) {
        out_states.push_back(get_snapshot(symbol_id));
    }
}

uint64_t SymbolCache::get_total_updates() const {
    uint64_t total = 0;
    for (const auto& state : states_) {
        // This read doesn't need to be perfectly consistent
        total += state.data.update_count;
    }
    return total;
}

} // namespace cache