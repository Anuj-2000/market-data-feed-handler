cmake_minimum_required(VERSION 3.16)
project(MarketDataFeedHandler VERSION 1.0 LANGUAGES CXX)

# C++17 required for modern features
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags for performance
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3 -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -fsanitize=address")

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Find required packages
find_package(Threads REQUIRED)

# Optional: Google Test for unit tests
find_package(GTest QUIET)

# Common library (shared between server and client)
add_library(common STATIC
    src/common/latency_tracker.cpp
    src/common/cache.cpp
    src/common/memory_pool.cpp
)
target_link_libraries(common Threads::Threads)

# Exchange Simulator (Server)
add_executable(exchange_simulator
    src/server/exchange_simulator.cpp
    src/server/tick_generator.cpp
    src/server/client_manager.cpp
    src/server/main_server.cpp
)
target_link_libraries(exchange_simulator common Threads::Threads)

# Feed Handler (Client)
add_executable(feed_handler
    src/client/feed_handler.cpp
    src/client/socket.cpp
    src/client/parser.cpp
    src/client/visualizer.cpp
    src/client/main_client.cpp
)
target_link_libraries(feed_handler common Threads::Threads)

# Unit tests (if GTest found)
if(GTest_FOUND)
    enable_testing()
    
    add_executable(unit_tests
        tests/test_parser.cpp
        tests/test_cache.cpp
        tests/test_tick_generator.cpp
    )
    target_link_libraries(unit_tests 
        common 
        GTest::GTest 
        GTest::Main
        Threads::Threads
    )
    
    add_test(NAME UnitTests COMMAND unit_tests)
endif()

# Benchmarks
add_executable(benchmark_latency
    benchmarks/benchmark_latency.cpp
)
target_link_libraries(benchmark_latency common Threads::Threads)

# Installation
install(TARGETS exchange_simulator feed_handler
    RUNTIME DESTINATION bin
)