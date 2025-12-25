#include "visualizer.h"
#include "protocol.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace ui {

TerminalVisualizer::TerminalVisualizer(const cache::SymbolCache& cache,
                                       const parser::ParserStats& parser_stats,
                                       const perf::LatencyStats& latency_stats)
    : cache_(cache),
      parser_stats_(parser_stats),
      latency_stats_(latency_stats),
      top_n_symbols_(20),
      update_interval_ms_(500),
      running_(false),
      start_time_ns_(0),
      last_update_time_ns_(0) {
}

TerminalVisualizer::~TerminalVisualizer() {
    stop();
}

void TerminalVisualizer::start() {
    running_ = true;
    start_time_ns_ = protocol::get_timestamp_ns();
    last_update_time_ns_ = start_time_ns_;
}

void TerminalVisualizer::stop() {
    running_ = false;
}

void TerminalVisualizer::update() {
    if (!running_) return;
    
    uint64_t now = protocol::get_timestamp_ns();
    uint64_t elapsed_ms = (now - last_update_time_ns_) / 1000000;
    
    if (elapsed_ms < update_interval_ms_) {
        return;  // Too soon to update
    }
    
    last_update_time_ns_ = now;
    
    // Draw UI
    clear_screen();
    move_cursor(0, 0);
    
    draw_header();
    draw_statistics();
    draw_symbol_table();
    draw_footer();
    
    std::cout << std::flush;
}

void TerminalVisualizer::clear_screen() {
    std::cout << "\033[2J";  // Clear entire screen
}

void TerminalVisualizer::move_cursor(int row, int col) {
    std::cout << "\033[" << row << ";" << col << "H";
}

void TerminalVisualizer::set_color(const std::string& color) {
    std::cout << color;
}

void TerminalVisualizer::reset_color() {
    std::cout << colors::RESET;
}

void TerminalVisualizer::draw_header() {
    set_color(colors::BOLD + colors::CYAN);
    std::cout << "=== NSE Market Data Feed Handler ===\n";
    reset_color();
    
    // Uptime
    uint64_t uptime_sec = (protocol::get_timestamp_ns() - start_time_ns_) / 1000000000ULL;
    uint64_t hours = uptime_sec / 3600;
    uint64_t minutes = (uptime_sec % 3600) / 60;
    uint64_t seconds = uptime_sec % 60;
    
    std::cout << "Uptime: " << std::setfill('0') 
              << std::setw(2) << hours << ":"
              << std::setw(2) << minutes << ":"
              << std::setw(2) << seconds;
    
    std::cout << " | Messages: " << format_number(parser_stats_.messages_parsed);
    
    // Calculate rate (messages per second over last interval)
    if (uptime_sec > 0) {
        uint64_t rate = parser_stats_.messages_parsed / uptime_sec;
        std::cout << " | Rate: " << format_number(rate) << " msg/s";
    }
    
    std::cout << "\n\n";
}

void TerminalVisualizer::draw_statistics() {
    set_color(colors::BOLD);
    std::cout << "Statistics:\n";
    reset_color();
    
    // Parser stats
    std::cout << "  Parser: ";
    std::cout << "Trades=" << parser_stats_.trades_parsed << " ";
    std::cout << "Quotes=" << parser_stats_.quotes_parsed << " ";
    std::cout << "Gaps=" << parser_stats_.sequence_gaps << " ";
    std::cout << "Errors=" << parser_stats_.checksum_errors << "\n";
    
    // Latency stats
    if (latency_stats_.sample_count > 0) {
        std::cout << "  Latency: ";
        std::cout << "p50=" << latency_stats_.p50_ns << "ns ";
        std::cout << "p99=" << latency_stats_.p99_ns << "ns ";
        std::cout << "p999=" << latency_stats_.p999_ns << "ns ";
        std::cout << "max=" << latency_stats_.max_ns << "ns\n";
    }
    
    // Cache stats
    uint64_t total_updates = cache_.get_total_updates();
    std::cout << "  Cache: Updates=" << format_number(total_updates) << "\n";
    
    std::cout << "\n";
}

void TerminalVisualizer::draw_symbol_table() {
    auto symbols = get_top_symbols();
    
    set_color(colors::BOLD);
    std::cout << "Top " << symbols.size() << " Symbols by Update Frequency:\n";
    reset_color();
    
    // Table header
    set_color(colors::BOLD);
    std::cout << std::left
              << std::setw(8) << "Symbol"
              << std::setw(12) << "Bid"
              << std::setw(12) << "Ask"
              << std::setw(12) << "LTP"
              << std::setw(12) << "Volume"
              << std::setw(10) << "Chg%"
              << std::setw(10) << "Updates"
              << "\n";
    
    std::cout << std::string(76, '-') << "\n";
    reset_color();
    
    // Table rows
    for (const auto& sym : symbols) {
        std::cout << std::left
                  << std::setw(8) << sym.symbol_id
                  << std::setw(12) << format_price(sym.bid)
                  << std::setw(12) << format_price(sym.ask)
                  << std::setw(12) << format_price(sym.last_price)
                  << std::setw(12) << format_number(sym.volume);
        
        // Color-coded change %
        set_color(get_change_color(sym.change_percent));
        std::cout << std::setw(10) << std::fixed << std::setprecision(2) 
                  << std::showpos << sym.change_percent << "%";
        reset_color();
        
        std::cout << std::setw(10) << format_number(sym.update_count)
                  << "\n";
    }
    
    std::cout << "\n";
}

void TerminalVisualizer::draw_footer() {
    set_color(colors::BOLD + colors::YELLOW);
    std::cout << "Press Ctrl+C to exit\n";
    reset_color();
}

std::vector<SymbolDisplayInfo> TerminalVisualizer::get_top_symbols() {
    std::vector<SymbolDisplayInfo> symbols;
    
    // Get all symbols from cache
    size_t num_symbols = cache_.get_num_symbols();
    
    for (size_t i = 0; i < num_symbols; ++i) {
        cache::MarketState state = cache_.get_snapshot(i);
        
        if (state.update_count == 0) {
            continue;  // Symbol never updated
        }
        
        SymbolDisplayInfo info;
        info.symbol_id = i;
        info.bid = state.best_bid;
        info.ask = state.best_ask;
        info.last_price = state.last_traded_price > 0 ? 
                         state.last_traded_price : 
                         (state.best_bid + state.best_ask) / 2.0;
        info.volume = state.last_traded_quantity;
        info.update_count = state.update_count;
        info.last_update_time = state.last_update_time;
        
        // Track initial price for % change
        if (initial_prices_.find(i) == initial_prices_.end()) {
            initial_prices_[i] = info.last_price;
        }
        
        // Calculate % change
        double initial_price = initial_prices_[i];
        if (initial_price > 0) {
            info.change_percent = ((info.last_price - initial_price) / initial_price) * 100.0;
        }
        
        symbols.push_back(info);
    }
    
    // Sort by update count (descending)
    std::sort(symbols.begin(), symbols.end(), 
              [](const SymbolDisplayInfo& a, const SymbolDisplayInfo& b) {
                  return a.update_count > b.update_count;
              });
    
    // Return top N
    if (symbols.size() > top_n_symbols_) {
        symbols.resize(top_n_symbols_);
    }
    
    return symbols;
}

std::string TerminalVisualizer::format_price(double price) const {
    if (price == 0.0) {
        return "-";
    }
    
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << price;
    return ss.str();
}

std::string TerminalVisualizer::format_number(uint64_t num) const {
    if (num < 1000) {
        return std::to_string(num);
    } else if (num < 1000000) {
        return std::to_string(num / 1000) + "K";
    } else if (num < 1000000000) {
        return std::to_string(num / 1000000) + "M";
    } else {
        return std::to_string(num / 1000000000) + "B";
    }
}

std::string TerminalVisualizer::get_change_color(double change_percent) const {
    if (change_percent > 0.01) {
        return colors::GREEN;
    } else if (change_percent < -0.01) {
        return colors::RED;
    } else {
        return colors::WHITE;
    }
}

} // namespace ui