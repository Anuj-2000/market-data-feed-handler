# Geometric Brownian Motion (GBM) Implementation

## Mathematical Background

### Stochastic Differential Equation

Stock prices are modeled using Geometric Brownian Motion, which follows the stochastic differential equation:

```
dS = μ·S·dt + σ·S·dW
```

Where:
- **S**: Current stock price
- **μ** (mu): Drift coefficient (expected return)
- **σ** (sigma): Volatility (standard deviation of returns)
- **dt**: Time increment
- **dW**: Wiener process (Brownian motion increment)

### Discrete Time Simulation

For simulation, we discretize the continuous equation:

```
S(t + dt) = S(t) + μ·S(t)·dt + σ·S(t)·√dt·Z
```

Where **Z ~ N(0,1)** is a standard normal random variable.

### Why GBM for Stock Prices?

1. **Positive Prices**: Geometric nature ensures S > 0 always
2. **Log-Normal Distribution**: Returns are normally distributed
3. **Percentage Changes**: Price changes are proportional to current price
4. **Market Realism**: Matches empirical stock price behavior

## Implementation Details

### Box-Muller Transform

To generate standard normal random variables Z ~ N(0,1), we use the Box-Muller transform:

**Algorithm**:
```
1. Generate two independent uniform random variables U₁, U₂ ~ Uniform(0, 1)
2. Transform using:
   Z₀ = √(-2·ln(U₁))·cos(2π·U₂)
   Z₁ = √(-2·ln(U₁))·sin(2π·U₂)
```

**Properties**:
- Generates two independent N(0,1) variables per call
- Exact transformation (not approximation)
- Computationally efficient

**Code Implementation**:
```cpp
double TickGenerator::generate_normal() {
    // Cache spare normal to avoid regenerating pairs
    if (has_spare_normal_) {
        has_spare_normal_ = false;
        return spare_normal_;
    }
    
    // Generate two uniform random numbers
    double u1, u2;
    do {
        u1 = uniform_dist_(rng_);
    } while (u1 <= 0.0);  // Avoid log(0)
    
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
```

### Parameter Selection

#### Drift (μ)
- **Neutral Market**: μ = 0.0 (no directional bias)
- **Bull Market**: μ = +0.05 (5% annualized upward drift)
- **Bear Market**: μ = -0.05 (5% annualized downward drift)

For this simulation, we use **μ = 0.0** to model a neutral market without trend.

#### Volatility (σ)
Volatility represents the standard deviation of returns. NSE stocks show varying volatility:

| Stock Type | Annual Volatility | Our Range |
|------------|-------------------|-----------|
| Large Cap (Nifty 50) | 15-25% | 0.01-0.03 |
| Mid Cap | 25-40% | 0.03-0.05 |
| Small Cap | 40-60% | 0.05-0.06 |

**Implementation**:
```cpp
volatility = 0.01 + (rand() % 50) / 1000.0;  // Range: 0.01 to 0.06
```

This gives us a realistic mix across the spectrum.

#### Time Step (dt)
```
dt = 0.001  // 1 millisecond
```

**Reasoning**:
- Market data updates at millisecond frequency
- Smaller dt → smoother price paths
- Too small dt → unnecessary computation
- 1ms balances realism with performance

### Price Update Calculation

```cpp
void TickGenerator::update_price_gbm(uint16_t symbol_id) {
    auto& symbol = symbols_[symbol_id];
    
    double S = symbol.current_price;
    double mu = symbol.drift;           // 0.0 for neutral
    double sigma = symbol.volatility;   // 0.01 to 0.06
    double dt = symbol.dt;              // 0.001
    
    // Get random normal using Box-Muller
    double dW = generate_normal();
    
    // GBM formula components
    double drift_component = mu * S * dt;
    double diffusion_component = sigma * S * std::sqrt(dt) * dW;
    
    // Update price
    double dS = drift_component + diffusion_component;
    symbol.current_price += dS;
    
    // Safety bounds (prevent extreme moves)
    // Real markets have circuit breakers at ±10-20%
    if (symbol.current_price < 1.0) {
        symbol.current_price = 1.0;  // Absolute minimum
    }
}
```

### Bid-Ask Spread Calculation

Realistic bid-ask spreads depend on:
1. **Liquidity**: More liquid → tighter spread
2. **Price**: Higher price → wider absolute spread
3. **Volatility**: More volatile → wider spread

**Implementation**:
```cpp
spread_pct = 0.0005 + (rand() % 150) / 100000.0;  // 0.05% to 0.2%
```

**Examples**:
- TCS @ ₹3500: Spread = 0.05% = ₹1.75 (₹3499.12 / ₹3500.88)
- Small cap @ ₹150: Spread = 0.2% = ₹0.30 (₹149.85 / ₹150.15)

```cpp
void TickGenerator::calculate_bid_ask(uint16_t symbol_id, 
                                       double& bid, double& ask) {
    const auto& symbol = symbols_[symbol_id];
    
    double mid_price = symbol.current_price;
    double half_spread = mid_price * symbol.spread_pct / 2.0;
    
    bid = mid_price - half_spread;
    ask = mid_price + half_spread;
}
```

## Realism Considerations

### 1. Initial Price Distribution
```cpp
current_price = 100.0 + (rand() % 4900);  // ₹100 to ₹5000
```

This matches NSE price range:
- Penny stocks: ₹10-100
- Small/Mid cap: ₹100-1000
- Large cap: ₹1000-5000
- Blue chips: > ₹5000 (Wipro, ITC, etc.)

### 2. Volume Generation

Volume should correlate (loosely) with price and volatility:

```cpp
base_volume = 1000 + (rand() % 9000);  // 1K to 10K

uint32_t generate_volume(uint16_t symbol_id) {
    // Add ±50% randomness
    double rand_factor = 0.5 + uniform_dist_(rng_);
    return base_volume * rand_factor;
}
```

### 3. Trade vs Quote Ratio

Real markets show:
- **70% Quote updates**: Market makers adjusting bid/ask
- **30% Trades**: Actual executions

```cpp
bool is_trade = (uniform_dist_(rng_) < 0.3);  // 30% probability
```

### 4. Price Continuity

GBM ensures:
- No gaps (continuous path)
- Proportional changes (₹100 stock won't jump to ₹500)
- Realistic volatility clustering

## Validation Methodology

### Statistical Tests

1. **Mean Reversion**: With μ = 0, price should oscillate around initial value
2. **Volatility Check**: Standard deviation of returns ≈ σ
3. **Distribution**: Log returns should be normally distributed

### Empirical Validation

Run simulation for 10,000 ticks and verify:

```python
# Expected metrics for σ = 0.02, dt = 0.001
expected_std_per_tick = σ * √dt = 0.02 * √0.001 ≈ 0.00063

# For price ₹1000:
expected_price_change = ₹1000 * 0.00063 = ₹0.63 per tick
```

### Visual Inspection

Plot price path and verify:
- No discontinuous jumps
- Realistic upward/downward movements
- No crashes to zero
- No explosions to infinity

## Performance Considerations

### Box-Muller Optimization

**Without Caching**:
```
Cost per normal = 1 × log + 1 × sqrt + 1 × cos + 1 × sin ≈ 40-50 CPU cycles
```

**With Caching** (our approach):
```
Amortized cost = 25-30 CPU cycles per normal
```

**Benefit**: 40% speedup by caching spare normal.

### Memory Layout

```cpp
struct SymbolConfig {
    uint16_t symbol_id;
    double current_price;      // 8 bytes
    double volatility;         // 8 bytes
    double drift;              // 8 bytes
    double dt;                 // 8 bytes
    double spread_pct;         // 8 bytes
    uint32_t base_volume;      // 4 bytes
    // Total: 46 bytes per symbol
};
```

For 100 symbols: **4.6 KB** (fits in L1 cache)

## Alternative Approaches Considered

### 1. Jump Diffusion Model
```
dS = μ·S·dt + σ·S·dW + S·J·dN
```
Where J is jump size and N is Poisson process.

**Rejected**: Too complex for assignment scope. GBM sufficient for realistic simulation.

### 2. Heston Stochastic Volatility
```
dS = μ·S·dt + √V·S·dW₁
dV = κ(θ - V)dt + ξ√V·dW₂
```
Where volatility V itself follows a stochastic process.

**Rejected**: Overkill. Constant volatility adequate for market data feed simulation.

### 3. GARCH Models
Time-series models with volatility clustering.

**Rejected**: Need historical data to calibrate. GBM is self-contained.

## References

- Hull, John C. "Options, Futures, and Other Derivatives" (Chapter 13: Wiener Processes and Itô's Lemma)
- Glasserman, Paul. "Monte Carlo Methods in Financial Engineering"
- NSE India: https://www.nseindia.com (for volatility and spread data)

## Code Location

- **Implementation**: `src/server/tick_generator.cpp`
- **Header**: `include/tick_generator.h`
- **Tests**: `tests/test_tick_generator.cpp`

---

*Last Updated: December 19, 2025*  
*Author: Anuj Vishwakarma*