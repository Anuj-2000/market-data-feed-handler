#include "exchange_simulator.h"
#include "protocol.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>

namespace server {

ExchangeSimulator::ExchangeSimulator(uint16_t port, size_t num_symbols)
    : port_(port),
      listen_fd_(-1),
      epoll_fd_(-1),
      running_(false),
      tick_generator_(std::make_unique<market::TickGenerator>()),
      num_symbols_(num_symbols),
      tick_rate_(100000),  // Default: 100K ticks/sec
      tick_interval_ns_(10000),  // 10 microseconds between ticks
      last_tick_time_ns_(0),
      total_messages_sent_(0),
      total_bytes_sent_(0),
      fault_injection_enabled_(false),
      next_symbol_index_(0) {
    
    // Initialize tick generator
    tick_generator_->initialize(num_symbols_);
}

ExchangeSimulator::~ExchangeSimulator() {
    stop();
}

bool ExchangeSimulator::start() {
    std::cout << "Starting Exchange Simulator on port " << port_ << "...\n";
    
    // Create listen socket
    if (!create_listen_socket()) {
        std::cerr << "Failed to create listen socket\n";
        return false;
    }
    
    // Setup epoll
    if (!setup_epoll()) {
        std::cerr << "Failed to setup epoll\n";
        close(listen_fd_);
        return false;
    }
    
    // Add listen socket to epoll
    if (!add_to_epoll(listen_fd_, EPOLLIN | EPOLLET)) {
        std::cerr << "Failed to add listen socket to epoll\n";
        close(epoll_fd_);
        close(listen_fd_);
        return false;
    }
    
    running_ = true;
    last_tick_time_ns_ = protocol::get_timestamp_ns();
    
    std::cout << "Server started successfully\n";
    std::cout << "Listening on port " << port_ << "\n";
    std::cout << "Ready to accept connections...\n";
    
    return true;
}

void ExchangeSimulator::stop() {
    if (!running_) return;
    
    std::cout << "\nStopping server...\n";
    running_ = false;
    
    // Close all client connections
    for (auto& client : clients_) {
        if (client.active) {
            close(client.fd);
        }
    }
    clients_.clear();
    
    // Close epoll and listen socket
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
    
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    
    std::cout << "Server stopped\n";
    std::cout << "Total messages sent: " << total_messages_sent_ << "\n";
    std::cout << "Total bytes sent: " << total_bytes_sent_ << "\n";
}

void ExchangeSimulator::run() {
    if (!running_) return;
    
    const int MAX_EVENTS = 64;
    struct epoll_event events[MAX_EVENTS];
    
    // Wait for events with 1ms timeout
    int num_events = epoll_wait(epoll_fd_, events, MAX_EVENTS, 1);
    
    if (num_events < 0) {
        if (errno != EINTR) {
            std::cerr << "epoll_wait error: " << strerror(errno) << "\n";
        }
        return;
    }
    
    // Handle events
    for (int i = 0; i < num_events; ++i) {
        int fd = events[i].data.fd;
        uint32_t event_mask = events[i].events;
        
        // Check for errors
        if (event_mask & (EPOLLERR | EPOLLHUP)) {
            if (fd == listen_fd_) {
                std::cerr << "Error on listen socket\n";
                running_ = false;
                return;
            } else {
                // Client disconnected or error
                handle_client_disconnect(fd);
                continue;
            }
        }
        
        // Handle incoming connection
        if (fd == listen_fd_ && (event_mask & EPOLLIN)) {
            handle_new_connection();
        }
    }
    
    // Generate and broadcast ticks at configured rate
    uint64_t current_time = protocol::get_timestamp_ns();
    if (current_time - last_tick_time_ns_ >= tick_interval_ns_) {
        generate_and_broadcast_tick();
        last_tick_time_ns_ = current_time;
    }
}

void ExchangeSimulator::set_tick_rate(uint32_t ticks_per_second) {
    tick_rate_ = ticks_per_second;
    if (tick_rate_ > 0) {
        tick_interval_ns_ = 1000000000ULL / tick_rate_;
    }
    std::cout << "Tick rate set to " << tick_rate_ << " ticks/sec\n";
    std::cout << "Tick interval: " << tick_interval_ns_ << " ns\n";
}

void ExchangeSimulator::enable_fault_injection(bool enable) {
    fault_injection_enabled_ = enable;
    std::cout << "Fault injection " << (enable ? "enabled" : "disabled") << "\n";
}

bool ExchangeSimulator::create_listen_socket() {
    // Create TCP socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "socket() failed: " << strerror(errno) << "\n";
        return false;
    }
    
    // Set socket options
    if (!set_socket_options(listen_fd_)) {
        close(listen_fd_);
        return false;
    }
    
    // Make non-blocking
    if (!make_socket_non_blocking(listen_fd_)) {
        close(listen_fd_);
        return false;
    }
    
    // Bind to port
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "bind() failed: " << strerror(errno) << "\n";
        close(listen_fd_);
        return false;
    }
    
    // Listen for connections
    if (listen(listen_fd_, SOMAXCONN) < 0) {
        std::cerr << "listen() failed: " << strerror(errno) << "\n";
        close(listen_fd_);
        return false;
    }
    
    return true;
}

bool ExchangeSimulator::make_socket_non_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        std::cerr << "fcntl(F_GETFL) failed: " << strerror(errno) << "\n";
        return false;
    }
    
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "fcntl(F_SETFL) failed: " << strerror(errno) << "\n";
        return false;
    }
    
    return true;
}

bool ExchangeSimulator::set_socket_options(int fd) {
    // SO_REUSEADDR allows quick restart
    int reuse = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << strerror(errno) << "\n";
        return false;
    }
    
    // TCP_NODELAY disables Nagle's algorithm for low latency
    int nodelay = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
        std::cerr << "setsockopt(TCP_NODELAY) failed: " << strerror(errno) << "\n";
        return false;
    }
    
    return true;
}

bool ExchangeSimulator::setup_epoll() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ < 0) {
        std::cerr << "epoll_create1() failed: " << strerror(errno) << "\n";
        return false;
    }
    return true;
}

bool ExchangeSimulator::add_to_epoll(int fd, uint32_t events) {
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.fd = fd;
    
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
        std::cerr << "epoll_ctl(ADD) failed: " << strerror(errno) << "\n";
        return false;
    }
    
    return true;
}

bool ExchangeSimulator::remove_from_epoll(int fd) {
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
        std::cerr << "epoll_ctl(DEL) failed: " << strerror(errno) << "\n";
        return false;
    }
    return true;
}

void ExchangeSimulator::handle_new_connection() {
    // Accept all pending connections (edge-triggered)
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No more connections to accept
                break;
            }
            std::cerr << "accept() failed: " << strerror(errno) << "\n";
            break;
        }
        
        // Make client socket non-blocking
        if (!make_socket_non_blocking(client_fd)) {
            close(client_fd);
            continue;
        }
        
        // Set socket options
        if (!set_socket_options(client_fd)) {
            close(client_fd);
            continue;
        }
        
        // Add to client list
        clients_.emplace_back(client_fd);
        
        std::cout << "New client connected: fd=" << client_fd 
                  << " from " << inet_ntoa(client_addr.sin_addr)
                  << ":" << ntohs(client_addr.sin_port)
                  << " (total clients: " << clients_.size() << ")\n";
    }
}

void ExchangeSimulator::handle_client_disconnect(int client_fd) {
    // Find and remove client
    for (size_t i = 0; i < clients_.size(); ++i) {
        if (clients_[i].fd == client_fd && clients_[i].active) {
            std::cout << "Client disconnected: fd=" << client_fd 
                      << " (sent " << clients_[i].messages_sent << " messages, "
                      << clients_[i].bytes_sent << " bytes)\n";
            
            cleanup_client(i);
            return;
        }
    }
}

void ExchangeSimulator::cleanup_client(size_t index) {
    if (index >= clients_.size()) return;
    
    auto& client = clients_[index];
    if (client.active) {
        close(client.fd);
        client.active = false;
    }
    
    // Remove from vector (swap with last and pop)
    if (index < clients_.size() - 1) {
        std::swap(clients_[index], clients_.back());
    }
    clients_.pop_back();
}

void ExchangeSimulator::generate_and_broadcast_tick() {
    // Round-robin through symbols
    uint16_t symbol_id = next_symbol_index_;
    next_symbol_index_ = (next_symbol_index_ + 1) % num_symbols_;
    
    generate_tick_for_symbol(symbol_id);
}

void ExchangeSimulator::generate_tick_for_symbol(uint16_t symbol_id) {
    protocol::MessageHeader header;
    
    // Generate tick (returns true for trade, false for quote)
    bool is_trade = tick_generator_->generate_tick(symbol_id, header);
    
    if (is_trade) {
        // Create and send trade message
        protocol::TradeMessage msg;
        msg.header = header;
        tick_generator_->fill_trade_payload(symbol_id, msg.payload);
        
        // Calculate checksum
        msg.checksum = protocol::calculate_checksum(&msg, sizeof(msg.header) + sizeof(msg.payload));
        
        // Broadcast to all clients
        broadcast_message(&msg, protocol::TradeMessage::SIZE);
        
    } else {
        // Create and send quote message
        protocol::QuoteMessage msg;
        msg.header = header;
        tick_generator_->fill_quote_payload(symbol_id, msg.payload);
        
        // Calculate checksum
        msg.checksum = protocol::calculate_checksum(&msg, sizeof(msg.header) + sizeof(msg.payload));
        
        // Broadcast to all clients
        broadcast_message(&msg, protocol::QuoteMessage::SIZE);
    }
}

void ExchangeSimulator::broadcast_message(const void* data, size_t len) {
    if (clients_.empty()) return;
    
    // Send to all connected clients
    for (size_t i = 0; i < clients_.size(); ) {
        auto& client = clients_[i];
        
        if (!client.active) {
            ++i;
            continue;
        }
        
        if (!send_to_client(client, data, len)) {
            // Client disconnected or send failed
            std::cout << "Failed to send to client fd=" << client.fd << ", disconnecting\n";
            cleanup_client(i);
            // Don't increment i, as cleanup_client removes this element
        } else {
            ++i;
        }
    }
    
    total_messages_sent_++;
    total_bytes_sent_ += len;
}

bool ExchangeSimulator::send_to_client(ClientConnection& client, const void* data, size_t len) {
    ssize_t sent = send(client.fd, data, len, MSG_NOSIGNAL);
    
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Send buffer full - client is slow
            // For now, we just skip this message (lossy)
            // In production, we'd buffer or disconnect slow clients
            return true;
        }
        
        // Connection error
        return false;
    }
    
    if (sent != static_cast<ssize_t>(len)) {
        // Partial send - for simplicity, treat as error
        // In production, we'd buffer remaining data
        return false;
    }
    
    // Update statistics
    client.messages_sent++;
    client.bytes_sent += len;
    client.last_send_time_ns = protocol::get_timestamp_ns();
    
    return true;
}

} // namespace server