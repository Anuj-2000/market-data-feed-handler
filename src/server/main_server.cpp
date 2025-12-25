#include "exchange_simulator.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <string>

// Global flag for graceful shutdown
std::atomic<bool> g_running(true);

void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        std::cout << "\nReceived signal " << signal << ", shutting down...\n";
        g_running = false;
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n";
    std::cout << "Options:\n";
    std::cout << "  -p PORT       Port to listen on (default: 9876)\n";
    std::cout << "  -s SYMBOLS    Number of symbols (default: 100)\n";
    std::cout << "  -r RATE       Tick rate in ticks/sec (default: 100000)\n";
    std::cout << "  -h            Show this help message\n";
    std::cout << "\nExample:\n";
    std::cout << "  " << program_name << " -p 9876 -s 100 -r 100000\n";
}

int main(int argc, char* argv[]) {
    // Default configuration
    uint16_t port = 9876;
    size_t num_symbols = 100;
    uint32_t tick_rate = 100000;  // 100K ticks/sec
    
    // Parse command line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "-p" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if (arg == "-s" && i + 1 < argc) {
            num_symbols = std::stoul(argv[++i]);
        }
        else if (arg == "-r" && i + 1 < argc) {
            tick_rate = std::stoul(argv[++i]);
        }
        else {
            std::cerr << "Unknown option: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    std::cout << "========================================\n";
    std::cout << "  Exchange Simulator (Market Data Feed)\n";
    std::cout << "========================================\n";
    std::cout << "Configuration:\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  Symbols: " << num_symbols << "\n";
    std::cout << "  Target tick rate: " << tick_rate << " ticks/sec\n";
    std::cout << "========================================\n\n";
    
    // Create and start server
    server::ExchangeSimulator simulator(port, num_symbols);
    simulator.set_tick_rate(tick_rate);
    
    if (!simulator.start()) {
        std::cerr << "Failed to start server\n";
        return 1;
    }
    
    // Statistics tracking
    auto start_time = std::chrono::steady_clock::now();
    uint64_t last_msg_count = 0;
    auto last_stat_time = start_time;
    
    // Main loop
    while (g_running) {
        simulator.run();
        
        // Print statistics every 5 seconds
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stat_time).count();
        
        if (elapsed >= 5) {
            uint64_t current_msg_count = simulator.get_total_messages_sent();
            uint64_t msgs_in_period = current_msg_count - last_msg_count;
            double msg_rate = msgs_in_period / static_cast<double>(elapsed);
            
            auto total_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
            
            std::cout << "[" << total_elapsed << "s] "
                      << "Clients: " << simulator.get_connected_clients() << " | "
                      << "Messages: " << current_msg_count << " | "
                      << "Rate: " << static_cast<uint64_t>(msg_rate) << " msg/s | "
                      << "Bytes: " << simulator.get_total_bytes_sent() << "\n";
            
            last_msg_count = current_msg_count;
            last_stat_time = now;
        }
        
        // Small sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::microseconds(10));
    }
    
    // Cleanup
    simulator.stop();
    
    auto end_time = std::chrono::steady_clock::now();
    auto total_time = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();
    
    std::cout << "\n========================================\n";
    std::cout << "  Server Statistics\n";
    std::cout << "========================================\n";
    std::cout << "Total runtime: " << total_time << " seconds\n";
    std::cout << "Total messages: " << simulator.get_total_messages_sent() << "\n";
    std::cout << "Total bytes: " << simulator.get_total_bytes_sent() << "\n";
    if (total_time > 0) {
        std::cout << "Average rate: " << (simulator.get_total_messages_sent() / total_time) << " msg/s\n";
    }
    std::cout << "========================================\n";
    
    return 0;
}