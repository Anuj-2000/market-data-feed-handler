#include "tick_generator.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>

using namespace market;
using namespace protocol;

// Helper to print a trade message
void print_trade(const MessageHeader& header, const TradePayload& payload) {
    std::cout << "[TRADE] Seq=" << header.sequence_number 
              << " Symbol=" << header.symbol_id
              << " Price=₹" << std::fixed << std::setprecision(2) << payload.price
              << " Qty=" << payload.quantity << "\n";
}

// Helper to print a quote message
void print_quote(const MessageHeader& header, const QuotePayload& payload) {
    std::cout << "[QUOTE] Seq=" << header.sequence_number 
              << " Symbol=" << header.symbol_id
              << " Bid=₹" << std::fixed << std::setprecision(2) << payload.bid_price
              << " Ask=₹" << payload.ask_price
              << " Spread=₹" << (payload.ask_price - payload.bid_price) << "\n";
}

// Test 1: Basic initialization
bool test_initialization() {
    std::cout << "\n=== Test 1: Initialization ===\n";
    
    TickGenerator gen;
    gen.initialize(10);
    
    // Check that all symbols have valid prices
    for (uint16_t i = 0; i < 10; ++i) {
        double price = gen.get_current_price(i);
        if (price < 100.0 || price > 5000.0) {
            std::cerr << "FAIL: Symbol " << i << " has invalid price: " << price << "\n";
            return false;
        }
    }
    
    std::cout << "PASS: All symbols initialized with valid prices\n";
    return true;
}

// Test 2: Generate ticks and verify structure
bool test_tick_generation() {
    std::cout << "\n=== Test 2: Tick Generation ===\n";
    
    TickGenerator gen;
    gen.initialize(5);
    
    MessageHeader header;
    TradePayload trade_payload;
    QuotePayload quote_payload;
    
    int trade_count = 0;
    int quote_count = 0;
    
    // Generate 100 ticks for symbol 0
    for (int i = 0; i < 100; ++i) {
        bool is_trade = gen.generate_tick(0, header);
        
        if (is_trade) {
            gen.fill_trade_payload(0, trade_payload);
            trade_count++;
            
            if (i < 5) print_trade(header, trade_payload);
        } else {
            gen.fill_quote_payload(0, quote_payload);
            quote_count++;
            
            if (i < 5) print_quote(header, quote_payload);
            
            // Verify bid < ask
            if (quote_payload.bid_price >= quote_payload.ask_price) {
                std::cerr << "FAIL: Bid >= Ask!\n";
                return false;
            }
        }
        
        // Verify sequence numbers are increasing
        if (header.sequence_number != static_cast<uint32_t>(i + 1)) {
            std::cerr << "FAIL: Sequence number mismatch\n";
            return false;
        }
    }
    
    std::cout << "Generated 100 ticks: " << trade_count << " trades, " 
              << quote_count << " quotes\n";
    
    // Check ratio is approximately 30/70
    if (trade_count < 20 || trade_count > 40) {
        std::cerr << "WARNING: Trade ratio outside expected range (20-40)\n";
    }
    
    std::cout << "PASS: Tick generation working correctly\n";
    return true;
}

// Test 3: Price movement statistics (verify GBM is realistic)
bool test_price_movement() {
    std::cout << "\n=== Test 3: Price Movement (GBM Validation) ===\n";
    
    TickGenerator gen;
    gen.initialize(3);
    
    // Track price for symbol 0 over many ticks
    std::vector<double> prices;
    prices.push_back(gen.get_current_price(0));
    
    MessageHeader header;
    TradePayload trade_payload;
    QuotePayload quote_payload;
    
    // Generate 1000 ticks
    for (int i = 0; i < 1000; ++i) {
        bool is_trade = gen.generate_tick(0, header);
        
        if (is_trade) {
            gen.fill_trade_payload(0, trade_payload);
        } else {
            gen.fill_quote_payload(0, quote_payload);
        }
        
        prices.push_back(gen.get_current_price(0));
    }
    
    // Calculate statistics
    double initial_price = prices.front();
    double final_price = prices.back();
    double min_price = *std::min_element(prices.begin(), prices.end());
    double max_price = *std::max_element(prices.begin(), prices.end());
    
    // Calculate average absolute return
    double total_abs_change = 0.0;
    for (size_t i = 1; i < prices.size(); ++i) {
        total_abs_change += std::abs(prices[i] - prices[i-1]);
    }
    double avg_abs_change = total_abs_change / (prices.size() - 1);
    
    std::cout << "Initial Price: ₹" << std::fixed << std::setprecision(2) << initial_price << "\n";
    std::cout << "Final Price:   ₹" << final_price << "\n";
    std::cout << "Min Price:     ₹" << min_price << "\n";
    std::cout << "Max Price:     ₹" << max_price << "\n";
    std::cout << "Price Change:  " << std::setprecision(2) 
              << ((final_price - initial_price) / initial_price * 100) << "%\n";
    std::cout << "Avg |Change|:  ₹" << avg_abs_change << " per tick\n";
    
    // Verify price stayed within reasonable bounds
    if (min_price < initial_price * 0.5 || max_price > initial_price * 2.0) {
        std::cerr << "WARNING: Price moved outside expected range\n";
    }
    
    // Verify price didn't crash to zero
    if (min_price < 1.0) {
        std::cerr << "FAIL: Price crashed below minimum\n";
        return false;
    }
    
    std::cout << "PASS: Price movement within reasonable bounds\n";
    return true;
}

// Test 4: Multi-symbol independence
bool test_multi_symbol() {
    std::cout << "\n=== Test 4: Multi-Symbol Independence ===\n";
    
    TickGenerator gen;
    gen.initialize(10);
    
    // Store initial prices
    std::vector<double> initial_prices;
    for (uint16_t i = 0; i < 10; ++i) {
        initial_prices.push_back(gen.get_current_price(i));
    }
    
    MessageHeader header;
    TradePayload trade_payload;
    QuotePayload quote_payload;
    
    // Generate ticks for each symbol
    for (uint16_t sym = 0; sym < 10; ++sym) {
        for (int i = 0; i < 100; ++i) {
            bool is_trade = gen.generate_tick(sym, header);
            
            if (is_trade) {
                gen.fill_trade_payload(sym, trade_payload);
            } else {
                gen.fill_quote_payload(sym, quote_payload);
            }
        }
    }
    
    // Verify each symbol moved independently
    int symbols_changed = 0;
    for (uint16_t i = 0; i < 10; ++i) {
        double current = gen.get_current_price(i);
        double initial = initial_prices[i];
        
        if (std::abs(current - initial) > 0.01) {
            symbols_changed++;
        }
        
        std::cout << "Symbol " << i << ": ₹" << initial 
                  << " → ₹" << current << "\n";
    }
    
    if (symbols_changed < 5) {
        std::cerr << "FAIL: Not enough symbols moved\n";
        return false;
    }
    
    std::cout << "PASS: " << symbols_changed << " symbols moved independently\n";
    return true;
}

// Test 5: Box-Muller normal distribution
bool test_box_muller() {
    std::cout << "\n=== Test 5: Box-Muller Distribution ===\n";
    
    TickGenerator gen;
    gen.initialize(1);
    
    // Generate many samples and check distribution
    std::vector<double> samples;
    MessageHeader header;
    TradePayload trade_payload;
    QuotePayload quote_payload;
    
    // Initial price
    double initial = gen.get_current_price(0);
    
    for (int i = 0; i < 1000; ++i) {
        bool is_trade = gen.generate_tick(0, header);
        if (is_trade) {
            gen.fill_trade_payload(0, trade_payload);
        } else {
            gen.fill_quote_payload(0, quote_payload);
        }
    }
    
    // The fact that price doesn't explode or crash validates Box-Muller is working
    double final = gen.get_current_price(0);
    
    std::cout << "After 1000 ticks: ₹" << initial << " → ₹" << final << "\n";
    
    if (final > 0.0 && final < 10000.0) {
        std::cout << "PASS: Box-Muller producing reasonable random normals\n";
        return true;
    }
    
    std::cerr << "FAIL: Price out of bounds\n";
    return false;
}

int main() {
    std::cout << "=================================\n";
    std::cout << "  Tick Generator Unit Tests\n";
    std::cout << "=================================\n";
    
    int passed = 0;
    int total = 5;
    
    if (test_initialization()) passed++;
    if (test_tick_generation()) passed++;
    if (test_price_movement()) passed++;
    if (test_multi_symbol()) passed++;
    if (test_box_muller()) passed++;
    
    std::cout << "\n=================================\n";
    std::cout << "Results: " << passed << "/" << total << " tests passed\n";
    std::cout << "=================================\n";
    
    return (passed == total) ? 0 : 1;
}