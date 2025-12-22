#include "protocol.h"
#include <iostream>
#include <cstring>
#include <iomanip>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "Ws2_32.lib")
    typedef int ssize_t;
#else
    #include <unistd.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif


// Simple blocking client to test server
class SimpleClient {
public:
    SimpleClient(const std::string& host, uint16_t port) 
        : host_(host), port_(port), sock_fd_(-1) {}
    
        ~SimpleClient() {
            if (sock_fd_ >= 0) {
    #ifdef _WIN32
                closesocket(sock_fd_);
    #else
                close(sock_fd_);
    #endif
            }
        }
    
    bool connect() {
        sock_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd_ < 0) {
            std::cerr << "socket() failed: " << strerror(errno) << "\n";
            return false;
        }
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        
        if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
            std::cerr << "Invalid address: " << host_ << "\n";
            return false;
        }
        
        if (::connect(sock_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "connect() failed: " << strerror(errno) << "\n";
            return false;
        }
        
        std::cout << "Connected to " << host_ << ":" << port_ << "\n";
        return true;
    }
    
    bool receive_message(uint8_t* buffer, size_t buffer_size, size_t& bytes_received) {
        // First, read the header to determine message type
        protocol::MessageHeader header;
        ssize_t n = recv(sock_fd_, reinterpret_cast<char*>(&header), sizeof(header), 0);
        
        if (n <= 0) {
            if (n == 0) {
                std::cout << "Server closed connection\n";
            } else {
                std::cerr << "recv() error: " << strerror(errno) << "\n";
            }
            return false;
        }
        
        if (n != sizeof(header)) {
            std::cerr << "Partial header received\n";
            return false;
        }
        
        // Copy header to buffer
        memcpy(buffer, &header, sizeof(header));
        size_t msg_size = protocol::get_message_size(header.msg_type);
        
        if (msg_size == 0 || msg_size > buffer_size) {
            std::cerr << "Invalid message size: " << msg_size << "\n";
            return false;
        }
        
        // Read the rest of the message
        size_t remaining = msg_size - sizeof(header);
        n = recv(sock_fd_, reinterpret_cast<char*>(buffer + sizeof(header)), remaining, 0);
        
        if (n != static_cast<ssize_t>(remaining)) {
            std::cerr << "Failed to receive full message\n";
            return false;
        }
        
        bytes_received = msg_size;
        return true;
    }
    
private:
    std::string host_;
    uint16_t port_;
    int sock_fd_;
};

void print_trade(const protocol::TradeMessage& msg) {
    std::cout << "[TRADE] Seq=" << msg.header.sequence_number
              << " Symbol=" << msg.header.symbol_id
              << " Price=Rs." << std::fixed << std::setprecision(2) << msg.payload.price
              << " Qty=" << msg.payload.quantity << "\n";
}

void print_quote(const protocol::QuoteMessage& msg) {
    std::cout << "[QUOTE] Seq=" << msg.header.sequence_number
              << " Symbol=" << msg.header.symbol_id
              << " Bid=Rs." << std::fixed << std::setprecision(2) << msg.payload.bid_price
              << " Ask=Rs." << msg.payload.ask_price
              << " Spread=Rs." << (msg.payload.ask_price - msg.payload.bid_price) << "\n";
}

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    uint16_t port = 9876;
    size_t num_messages = 100;
    
    // Parse arguments
    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::strtol(argv[2], nullptr, 10));
    }
    if (argc > 3) {
        num_messages = std::strtoul(argv[3], nullptr, 10);
    }
    
    std::cout << "=== Simple Test Client ===\n";
    std::cout << "Connecting to " << host << ":" << port << "\n";
    std::cout << "Will receive " << num_messages << " messages\n\n";
    
    SimpleClient client(host, port);
    
    if (!client.connect()) {
        return 1;
    }
    
    uint8_t buffer[1024];
    size_t messages_received = 0;
    size_t trade_count = 0;
    size_t quote_count = 0;
    
    while (messages_received < num_messages) {
        size_t bytes_received = 0;
        
        if (!client.receive_message(buffer, sizeof(buffer), bytes_received)) {
            break;
        }
        
        // Parse message header
        protocol::MessageHeader* header = reinterpret_cast<protocol::MessageHeader*>(buffer);
        
        // Print first 10 messages for debugging
        if (messages_received < 10) {
            if (header->msg_type == protocol::MessageType::TRADE) {
                protocol::TradeMessage* msg = reinterpret_cast<protocol::TradeMessage*>(buffer);
                print_trade(*msg);
                trade_count++;
            } else if (header->msg_type == protocol::MessageType::QUOTE) {
                protocol::QuoteMessage* msg = reinterpret_cast<protocol::QuoteMessage*>(buffer);
                print_quote(*msg);
                quote_count++;
            }
        } else {
            // Just count
            if (header->msg_type == protocol::MessageType::TRADE) {
                trade_count++;
            } else if (header->msg_type == protocol::MessageType::QUOTE) {
                quote_count++;
            }
        }
        
        messages_received++;
        
        // Print progress every 10 messages
        if (messages_received % 10 == 0) {
            std::cout << "Received " << messages_received << " messages...\n";
        }
    }
    
    std::cout << "\n=== Statistics ===\n";
    std::cout << "Total messages: " << messages_received << "\n";
    std::cout << "Trades: " << trade_count << " (" 
              << (100.0 * trade_count / messages_received) << "%)\n";
    std::cout << "Quotes: " << quote_count << " (" 
              << (100.0 * quote_count / messages_received) << "%)\n";
    std::cout << "Test complete!\n";
    
    return 0;
}