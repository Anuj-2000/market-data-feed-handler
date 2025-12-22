#!/bin/bash

echo "Building..."
g++ -std=c++17 -O2 -D_USE_MATH_DEFINES -I./include \
    src/server/main_server.cpp \
    src/server/exchange_simulator.cpp \
    src/server/tick_generator.cpp \
    -o exchange_simulator -pthread || exit 1

g++ -std=c++17 -O2 -D_USE_MATH_DEFINES -I./include \
    tests/simple_test_client.cpp \
    -o test_client || exit 1

echo "Build complete!"
echo ""
echo "Starting server in background..."
./exchange_simulator -r 10000 &
SERVER_PID=$!

echo "Waiting for server to start..."
sleep 2

echo "Running client test..."
./test_client 127.0.0.1 9876 50

echo ""
echo "Stopping server..."
kill $SERVER_PID
wait $SERVER_PID 2>/dev/null

echo "Test complete!"