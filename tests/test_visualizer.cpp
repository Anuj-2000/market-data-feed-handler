#include "visualizer.h"
#include "cache.h"
#include "parser.h"
#include "latency_tracker.h"
#include "tick_generator.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

std::atomic<bool> g_running(true);

void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nShutting down...\n";
        g_running = false;
    }
}

int main() {
    // Setup signal handler
    signal(SIGINT, signal_handler);
    
    std::cout << "Starting visualization demo...\n";
    std::cout << "Press Ctrl+C to exit\n\n";
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Initialize components
    cache::SymbolCache cache(50);
    parser::ParserStats parser_stats;
    perf::LatencyStats latency_stats;
    perf::LatencyTracker latency_tracker(100, 1000000);
    
    // Initialize tick generator for simulation
    market::TickGenerator tick_gen;
    tick_gen.initialize(50);
    
    // Create visualizer
    ui::TerminalVisualizer viz(cache, parser_stats, latency_stats);
    viz.set_top_n_symbols(20);
    viz.set_update_interval_ms(500);
    viz.start();
    
    // Simulation loop
    uint32_t message_count = 0;
    
    while (g_running) {
        // Simulate tick generation and processing
        for (int i = 0; i < 100; ++i) {
            uint16_t symbol = rand() % 50;
            
            // Measure processing latency
            auto start = perf::get_timestamp_ns();
            
            // Generate tick
            protocol::MessageHeader header;
            bool is_trade = tick_gen.generate_tick(symbol, header);
            
            if (is_trade) {
                protocol::TradePayload payload;
                tick_gen.fill_trade_payload(symbol, payload);
                
                // Update cache
                cache.update_trade(symbol, payload.price, payload.quantity);
                
                // Update parser stats
                parser_stats.trades_parsed++;
            } else {
                protocol::QuotePayload payload;
                tick_gen.fill_quote_payload(symbol, payload);
                
                // Update cache
                cache.update_quote(symbol, 
                                  payload.bid_price, payload.bid_quantity,
                                  payload.ask_price, payload.ask_quantity);
                
                // Update parser stats
                parser_stats.quotes_parsed++;
            }
            
            parser_stats.messages_parsed++;
            message_count++;
            
            // Record latency
            auto end = perf::get_timestamp_ns();
            latency_tracker.record(end - start);
        }
        
        // Update latency stats for display
        latency_stats = latency_tracker.get_stats();
        
        // Update visualization
        viz.update();
        
        // Sleep to simulate message rate
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    viz.stop();
    
    std::cout << "\n\nFinal Statistics:\n";
    std::cout << "Total messages: " << message_count << "\n";
    std::cout << "Trades: " << parser_stats.trades_parsed << "\n";
    std::cout << "Quotes: " << parser_stats.quotes_parsed << "\n";
    
    return 0;
}