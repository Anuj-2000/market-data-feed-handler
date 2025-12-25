#pragma once

#include <cstdint>
#include <vector>
#include <atomic>
#include <memory>
#include "tick_generator.h"

namespace server {

// Client connection state
struct ClientConnection {
    int fd;                         // Socket file descriptor
    bool active;                    // Is this connection alive?
    uint64_t messages_sent;         // Statistics
    uint64_t bytes_sent;
    uint64_t last_send_time_ns;     // For detecting slow clients
    
    ClientConnection(int socket_fd) 
        : fd(socket_fd), 
          active(true), 
          messages_sent(0),
          bytes_sent(0),
          last_send_time_ns(0) {}
};

class ExchangeSimulator {
public:
    // Initialize with port and number of symbols
    ExchangeSimulator(uint16_t port, size_t num_symbols = 100);
    ~ExchangeSimulator();
    
    // Start accepting connections (non-blocking)
    bool start();
    
    // Main event loop - call this in a loop
    void run();
    
    // Stop the server gracefully
    void stop();
    
    // Configuration
    void set_tick_rate(uint32_t ticks_per_second);
    void enable_fault_injection(bool enable);
    
    // Statistics
    size_t get_connected_clients() const { return clients_.size(); }
    uint64_t get_total_messages_sent() const { return total_messages_sent_; }
    uint64_t get_total_bytes_sent() const { return total_bytes_sent_; }
    
private:
    // Socket management
    bool create_listen_socket();
    bool make_socket_non_blocking(int fd);
    bool set_socket_options(int fd);
    
    // epoll management
    bool setup_epoll();
    bool add_to_epoll(int fd, uint32_t events);
    bool remove_from_epoll(int fd);
    
    // Connection handling
    void handle_new_connection();
    void handle_client_disconnect(int client_fd);
    void cleanup_client(size_t index);
    
    // Message generation and broadcasting
    void generate_and_broadcast_tick();
    void broadcast_message(const void* data, size_t len);
    bool send_to_client(ClientConnection& client, const void* data, size_t len);
    
    // Tick generation
    void generate_tick_for_symbol(uint16_t symbol_id);
    
    // Server state
    uint16_t port_;
    int listen_fd_;
    int epoll_fd_;
    std::atomic<bool> running_;
    
    // Tick generation
    std::unique_ptr<market::TickGenerator> tick_generator_;
    size_t num_symbols_;
    uint32_t tick_rate_;              // Ticks per second
    uint64_t tick_interval_ns_;       // Nanoseconds between ticks
    uint64_t last_tick_time_ns_;
    
    // Client management
    std::vector<ClientConnection> clients_;
    
    // Statistics
    std::atomic<uint64_t> total_messages_sent_;
    std::atomic<uint64_t> total_bytes_sent_;
    
    // Configuration
    bool fault_injection_enabled_;
    
    // Next symbol to generate tick for (round-robin)
    size_t next_symbol_index_;
};

} // namespace server