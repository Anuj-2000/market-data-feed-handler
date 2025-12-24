#include "protocol.h"
#include "tick_generator.h"
#include <iostream>
#include <iomanip>

int main() {
    std::cout << "=== GBM Tick Generator Test ===\n\n";
    
    // Initialize generator with 5 symbols
    market::TickGenerator gen;
    gen.initialize(5);
    
    std::cout << "\nGenerating 20 ticks for symbol 0...\n\n";
    
    protocol::MessageHeader header;
    protocol::TradePayload trade_payload;
    protocol::QuotePayload quote_payload;
    
    double initial_price = gen.get_current_price(0);
    std::cout << "Initial price: Rs." << std::fixed << std::setprecision(2) 
              << initial_price << "\n\n";
    
    // Generate 20 ticks
    for (int i = 0; i < 20; ++i) {
        bool is_trade = gen.generate_tick(0, header);
        
        std::cout << "Tick " << std::setw(2) << (i+1) << " | Seq=" << header.sequence_number;
        
        if (is_trade) {
            gen.fill_trade_payload(0, trade_payload);
            std::cout << " | [TRADE] Price=Rs." << std::setw(8) << trade_payload.price
                      << " Qty=" << trade_payload.quantity << "\n";
        } else {
            gen.fill_quote_payload(0, quote_payload);
            std::cout << " | [QUOTE] Bid=Rs." << std::setw(8) << quote_payload.bid_price
                      << " Ask=Rs." << std::setw(8) << quote_payload.ask_price
                      << " Spread=Rs." << std::setw(6) 
                      << (quote_payload.ask_price - quote_payload.bid_price) << "\n";
        }
    }
    
    double final_price = gen.get_current_price(0);
    double change_pct = ((final_price - initial_price) / initial_price) * 100;
    
    std::cout << "\nFinal price: Rs." << final_price << "\n";
    std::cout << "Change: " << std::showpos << change_pct << std::noshowpos << "%\n";
    std::cout << "\nGBM tick generator working!\n";
    
    return 0;
}