#pragma once

#include <random>
#include <cmath>
#include "protocol.h"

namespace market {

// Symbol configuration
struct SymbolConfig {
    uint16_t symbol_id;
    double current_price;
    double volatility;      // σ (sigma) - typically 0.01 to 0.06
    double drift;           // μ (mu) - 0.0 for neutral, +/-0.05 for trend
    double dt;              // time step (e.g., 0.001 for 1ms)
    double spread_pct;      // bid-ask spread as % of price (0.05% - 0.2%)
    uint32_t base_volume;   // Base trading volume
    
    SymbolConfig(uint16_t id) 
        : symbol_id(id),
          current_price(100.0 + (rand() % 4900)),  // ₹100 to ₹5000
          volatility(0.01 + (rand() % 50) / 1000.0), // 0.01 to 0.06
          drift(0.0),
          dt(0.001),  // 1ms time step
          spread_pct(0.0005 + (rand() % 150) / 100000.0), // 0.05% to 0.2%
          base_volume(1000 + (rand() % 9000)) {}
};

class TickGenerator {
public:
    TickGenerator();
    
    // Initialize with number of symbols
    void initialize(size_t num_symbols);
    
    // Generate next tick for a symbol (returns true for trade, false for quote)
    bool generate_tick(uint16_t symbol_id, protocol::MessageHeader& header);
    
    // Fill trade payload
    void fill_trade_payload(uint16_t symbol_id, protocol::TradePayload& payload);
    
    // Fill quote payload
    void fill_quote_payload(uint16_t symbol_id, protocol::QuotePayload& payload);
    
    // Get current price for symbol
    double get_current_price(uint16_t symbol_id) const;
    
private:
    // Geometric Brownian Motion price update
    void update_price_gbm(uint16_t symbol_id);
    
    // Box-Muller transform for normal distribution
    double generate_normal();
    
    // Calculate bid/ask from mid price
    void calculate_bid_ask(uint16_t symbol_id, double& bid, double& ask);
    
    // Generate trade volume
    uint32_t generate_volume(uint16_t symbol_id);
    
    std::vector<SymbolConfig> symbols_;
    
    // Random number generation
    std::mt19937 rng_;
    std::uniform_real_distribution<double> uniform_dist_;
    
    // For Box-Muller transform
    bool has_spare_normal_;
    double spare_normal_;
    
    // Sequence number tracking
    uint32_t sequence_number_;
};

} // namespace market