#pragma once

#include "cache.h"
#include "parser.h"
#include "latency_tracker.h"
#include <string>
#include <vector>
#include <map>
#include <atomic>

namespace ui {

// Symbol display info
struct SymbolDisplayInfo {
    uint16_t symbol_id;
    double bid;
    double ask;
    double last_price;
    uint64_t volume;
    double change_percent;
    uint64_t update_count;
    uint64_t last_update_time;
    
    SymbolDisplayInfo() : symbol_id(0), bid(0.0), ask(0.0), last_price(0.0),
                         volume(0), change_percent(0.0), update_count(0),
                         last_update_time(0) {}
};

class TerminalVisualizer {
public:
    TerminalVisualizer(const cache::SymbolCache& cache,
                       const parser::ParserStats& parser_stats,
                       const perf::LatencyStats& latency_stats);
    
    ~TerminalVisualizer();
    
    // Update display (call periodically, e.g., every 500ms)
    void update();
    
    // Configuration
    void set_top_n_symbols(size_t n) { top_n_symbols_ = n; }
    void set_update_interval_ms(uint64_t ms) { update_interval_ms_ = ms; }
    
    // Control
    void start();
    void stop();
    bool is_running() const { return running_; }
    
private:
    // Display functions
    void clear_screen();
    void move_cursor(int row, int col);
    void set_color(const std::string& color);
    void reset_color();
    
    void draw_header();
    void draw_statistics();
    void draw_symbol_table();
    void draw_footer();
    
    // Helper functions
    std::vector<SymbolDisplayInfo> get_top_symbols();
    std::string format_price(double price) const;
    std::string format_number(uint64_t num) const;
    std::string get_change_color(double change_percent) const;
    
    // References to data sources
    const cache::SymbolCache& cache_;
    const parser::ParserStats& parser_stats_;
    const perf::LatencyStats& latency_stats_;
    
    // Configuration
    size_t top_n_symbols_;
    uint64_t update_interval_ms_;
    
    // State
    std::atomic<bool> running_;
    uint64_t start_time_ns_;
    uint64_t last_update_time_ns_;
    
    // Track initial prices for % change calculation
    std::map<uint16_t, double> initial_prices_;
};

// ANSI color codes
namespace colors {
    const std::string RESET = "\033[0m";
    const std::string BOLD = "\033[1m";
    const std::string RED = "\033[31m";
    const std::string GREEN = "\033[32m";
    const std::string YELLOW = "\033[33m";
    const std::string BLUE = "\033[34m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN = "\033[36m";
    const std::string WHITE = "\033[37m";
    
    const std::string BG_BLACK = "\033[40m";
    const std::string BG_RED = "\033[41m";
    const std::string BG_GREEN = "\033[42m";
}

} // namespace ui