#include "tick_generator.h"
#include <cmath>
#include <ctime>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace market
{

    TickGenerator::TickGenerator()
        : rng_(std::random_device{}()),
          uniform_dist_(0.0, 1.0),
          has_spare_normal_(false),
          spare_normal_(0.0),
          sequence_number_(0)
    {
    }

    void TickGenerator::initialize(size_t num_symbols)
    {
        symbols_.clear();
        symbols_.reserve(num_symbols);

        // Seed random with current time for variety
        srand(time(nullptr));

        for (size_t i = 0; i < num_symbols; ++i)
        {
            symbols_.emplace_back(static_cast<uint16_t>(i));
        }

        std::cout << "Initialized " << num_symbols << " symbols with GBM parameters\n";

        // Print sample symbols for debugging
        if (num_symbols >= 3)
        {
            std::cout << "Sample symbols:\n";
            for (size_t i = 0; i < 3; ++i)
            {
                const auto &sym = symbols_[i];
                std::cout << "  Symbol " << sym.symbol_id
                          << ": Price=Rs." << sym.current_price // Changed from ₹
                          << ", Vol=" << sym.volatility
                          << ", Spread=" << (sym.spread_pct * 100) << "%\n";
            }
        }
    }

    bool TickGenerator::generate_tick(uint16_t symbol_id, protocol::MessageHeader &header)
    {
        if (symbol_id >= symbols_.size())
        {
            return false;
        }

        // Update price using GBM
        update_price_gbm(symbol_id);

        // Decide if this is a trade or quote (30% trades, 70% quotes)
        double rand_val = uniform_dist_(rng_);
        bool is_trade = (rand_val < 0.3);

        // Fill header
        header.msg_type = is_trade ? protocol::MessageType::TRADE : protocol::MessageType::QUOTE;
        header.sequence_number = ++sequence_number_;
        header.timestamp_ns = protocol::get_timestamp_ns();
        header.symbol_id = symbol_id;

        return is_trade;
    }

    void TickGenerator::fill_trade_payload(uint16_t symbol_id, protocol::TradePayload &payload)
    {
        if (symbol_id >= symbols_.size())
        {
            return;
        }

        const auto &symbol = symbols_[symbol_id];

        // Trade happens at mid price (between bid and ask)
        payload.price = symbol.current_price;
        payload.quantity = generate_volume(symbol_id);
    }

    void TickGenerator::fill_quote_payload(uint16_t symbol_id, protocol::QuotePayload &payload)
    {
        if (symbol_id >= symbols_.size())
        {
            return;
        }

        double bid, ask;
        calculate_bid_ask(symbol_id, bid, ask);

        payload.bid_price = bid;
        payload.bid_quantity = generate_volume(symbol_id);
        payload.ask_price = ask;
        payload.ask_quantity = generate_volume(symbol_id);
    }

    double TickGenerator::get_current_price(uint16_t symbol_id) const
    {
        if (symbol_id >= symbols_.size())
        {
            return 0.0;
        }
        return symbols_[symbol_id].current_price;
    }

    void TickGenerator::update_price_gbm(uint16_t symbol_id)
    {
        auto &symbol = symbols_[symbol_id];

        // Geometric Brownian Motion:
        // dS = μ * S * dt + σ * S * sqrt(dt) * dW
        // Where dW ~ N(0, 1)

        double S = symbol.current_price;
        double mu = symbol.drift;
        double sigma = symbol.volatility;
        double dt = symbol.dt;

        // Get random normal using Box-Muller
        double dW = generate_normal();

        // Calculate price change
        double drift_component = mu * S * dt;
        double diffusion_component = sigma * S * std::sqrt(dt) * dW;

        // Update price
        double dS = drift_component + diffusion_component;
        symbol.current_price += dS;

        // Ensure price doesn't go negative or too extreme
        // NSE has circuit breakers at ±10% for most stocks
        const double min_price = symbol.current_price * 0.5; // Don't drop below 50%
        const double max_price = symbol.current_price * 2.0; // Don't exceed 200%

        if (symbol.current_price < min_price)
        {
            symbol.current_price = min_price;
        }
        else if (symbol.current_price > max_price)
        {
            symbol.current_price = max_price;
        }

        // Absolute minimum to prevent crashes
        if (symbol.current_price < 1.0)
        {
            symbol.current_price = 1.0;
        }
    }

    double TickGenerator::generate_normal()
    {
        // Box-Muller transform to generate standard normal distribution
        // Generates pairs of independent normals, cache the spare

        if (has_spare_normal_)
        {
            has_spare_normal_ = false;
            return spare_normal_;
        }

        // Generate two uniform random numbers
        double u1, u2;
        do
        {
            u1 = uniform_dist_(rng_);
        } while (u1 <= 0.0); // Avoid log(0)

        u2 = uniform_dist_(rng_);

        // Box-Muller transformation
        double r = std::sqrt(-2.0 * std::log(u1));
        double theta = 2.0 * M_PI * u2;

        // Generate two independent normals
        double z0 = r * std::cos(theta);
        double z1 = r * std::sin(theta);

        // Cache one for next call
        spare_normal_ = z1;
        has_spare_normal_ = true;

        return z0;
    }

    void TickGenerator::calculate_bid_ask(uint16_t symbol_id, double &bid, double &ask)
    {
        const auto &symbol = symbols_[symbol_id];

        double mid_price = symbol.current_price;
        double half_spread = mid_price * symbol.spread_pct / 2.0;

        bid = mid_price - half_spread;
        ask = mid_price + half_spread;

        // Ensure bid < ask (should always be true, but floating point...)
        if (bid >= ask)
        {
            bid = mid_price - 0.01;
            ask = mid_price + 0.01;
        }
    }

    uint32_t TickGenerator::generate_volume(uint16_t symbol_id)
    {
        const auto &symbol = symbols_[symbol_id];

        // Generate volume with some randomness
        // Base volume ± 50%
        double rand_factor = 0.5 + uniform_dist_(rng_); // 0.5 to 1.5
        uint32_t volume = static_cast<uint32_t>(symbol.base_volume * rand_factor);

        // Ensure minimum volume
        if (volume < 100)
        {
            volume = 100;
        }

        return volume;
    }

} // namespace market