#pragma once

#include <cstdint>
#include <cstring>
#include <chrono>

namespace protocol {

// Message types
enum class MessageType : uint16_t {
    TRADE = 0x01,
    QUOTE = 0x02,
    HEARTBEAT = 0x03,
    SUBSCRIBE = 0xFF  // Client -> Server subscription
};

// Message header (16 bytes)
struct MessageHeader {
    MessageType msg_type;      // 2 bytes
    uint32_t sequence_number;  // 4 bytes
    uint64_t timestamp_ns;     // 8 bytes (nanoseconds since epoch)
    uint16_t symbol_id;        // 2 bytes (0-499)
    
    MessageHeader() : msg_type(MessageType::HEARTBEAT), 
                      sequence_number(0), 
                      timestamp_ns(0), 
                      symbol_id(0) {}
} __attribute__((packed));

// Trade payload (12 bytes)
struct TradePayload {
    double price;          // 8 bytes
    uint32_t quantity;     // 4 bytes
} __attribute__((packed));

// Quote payload (28 bytes)
struct QuotePayload {
    double bid_price;      // 8 bytes
    uint32_t bid_quantity; // 4 bytes
    double ask_price;      // 8 bytes
    uint32_t ask_quantity; // 4 bytes
} __attribute__((packed));

// Complete message structures
struct TradeMessage {
    MessageHeader header;
    TradePayload payload;
    uint32_t checksum;     // XOR of all previous bytes
    
    static constexpr size_t SIZE = sizeof(MessageHeader) + sizeof(TradePayload) + sizeof(uint32_t);
} __attribute__((packed));

struct QuoteMessage {
    MessageHeader header;
    QuotePayload payload;
    uint32_t checksum;
    
    static constexpr size_t SIZE = sizeof(MessageHeader) + sizeof(QuotePayload) + sizeof(uint32_t);
} __attribute__((packed));

struct HeartbeatMessage {
    MessageHeader header;
    uint32_t checksum;
    
    static constexpr size_t SIZE = sizeof(MessageHeader) + sizeof(uint32_t);
} __attribute__((packed));

// Subscription message (Client -> Server)
struct SubscriptionHeader {
    uint8_t command;       // 0xFF for subscribe
    uint16_t count;        // Number of symbols
    // Followed by array of uint16_t symbol_ids
} __attribute__((packed));

// Utility functions
inline uint32_t calculate_checksum(const void* data, size_t len) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t checksum = 0;
    for (size_t i = 0; i < len; ++i) {
        checksum ^= bytes[i];
    }
    return checksum;
}

inline bool verify_checksum(const void* data, size_t total_len) {
    if (total_len < sizeof(uint32_t)) return false;
    
    size_t data_len = total_len - sizeof(uint32_t);
    uint32_t received_checksum;
    std::memcpy(&received_checksum, 
                static_cast<const uint8_t*>(data) + data_len, 
                sizeof(uint32_t));
    
    uint32_t calculated = calculate_checksum(data, data_len);
    return calculated == received_checksum;
}

inline size_t get_message_size(MessageType type) {
    switch (type) {
        case MessageType::TRADE:
            return TradeMessage::SIZE;
        case MessageType::QUOTE:
            return QuoteMessage::SIZE;
        case MessageType::HEARTBEAT:
            return HeartbeatMessage::SIZE;
        default:
            return 0;
    }
}

// Helper to get current time in nanoseconds
inline uint64_t get_timestamp_ns() {
    auto now = std::chrono::high_resolution_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

} // namespace protocol