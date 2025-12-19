cd # Market Data Feed Handler

Low-latency multi-asset market data processor for NSE Co-location.

## Overview

Real-time market data feed handler consisting of:
- **Exchange Simulator**: Generates realistic market ticks using Geometric Brownian Motion
- **Feed Handler Client**: Receives, parses, and processes binary market data with minimal latency
- **Terminal Visualization**: Live market data display with statistics

## Quick Start

```bash
# Build the project
./scripts/build.sh

# Run demo (starts server and client)
./scripts/run_demo.sh

# Or run separately
./scripts/run_server.sh
./scripts/run_client.sh
```

## System Requirements

- Linux (Ubuntu 20.04+ or similar)
- GCC 9+ or Clang 10+ with C++17 support
- CMake 3.16+
- pthread support

## Architecture

```
Exchange Simulator (Server)    →    Feed Handler (Client)
  - Geometric Brownian Motion        - TCP Client (epoll)
  - TCP Server (epoll)               - Binary Parser
  - Multi-client support             - Lock-free Cache
  - 100 symbols                      - Terminal UI
  - 10K-500K msgs/sec                - Latency tracking
```

## Performance Targets

- **Throughput**: 100K+ messages/second
- **Latency**: p99 < 50μs end-to-end
- **Parser**: Zero-copy, < 30ns per message
- **Cache**: Lock-free reads, < 50ns

## Documentation

- [DESIGN.md](docs/DESIGN.md) - Architecture and design decisions
- [GBM.md](docs/GBM.md) - Geometric Brownian Motion implementation
- [NETWORK.md](docs/NETWORK.md) - Network layer details
- [PERFORMANCE.md](docs/PERFORMANCE.md) - Benchmark results
- [QUESTIONS.md](docs/QUESTIONS.md) - Critical thinking answers

## Author

Anuj Vishwakarma
