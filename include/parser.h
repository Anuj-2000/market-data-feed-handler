#pragma once

#include "protocol.h"
#include <cstdint>
#include <cstring>
#include <functional>

namespace parser {

// Parser statistics
struct ParserStats {
    uint64_t messages_parsed;
    uint64_t trades_parsed;
    uint64_t quotes_parsed;
    uint64_t heartbeats_parsed;
    uint64_t sequence_gaps;
    uint64_t checksum_errors;
    uint64_t malformed_messages;
    
    ParserStats() : messages_parsed(0), trades_parsed(0), quotes_parsed(0),
                    heartbeats_parsed(0), sequence_gaps(0), 
                    checksum_errors(0), malformed_messages(0) {}
};

// Callback types for parsed messages
using TradeCallback = std::function<void(const protocol::TradeMessage&)>;
using QuoteCallback = std::function<void(const protocol::QuoteMessage&)>;
using HeartbeatCallback = std::function<void(const protocol::HeartbeatMessage&)>;

class MessageParser {
public:
    MessageParser();
    ~MessageParser();
    
    // Set callbacks for different message types
    void set_trade_callback(TradeCallback callback);
    void set_quote_callback(QuoteCallback callback);
    void set_heartbeat_callback(HeartbeatCallback callback);
    
    // Parse data from TCP stream
    // Returns number of bytes consumed
    size_t parse(const uint8_t* data, size_t len);
    
    // Reset parser state (call on reconnect)
    void reset();
    
    // Statistics
    const ParserStats& get_stats() const { return stats_; }
    
    // Configuration
    void set_validate_checksum(bool validate) { validate_checksum_ = validate; }
    void set_validate_sequence(bool validate) { validate_sequence_ = validate; }
    
private:
    // Parse a complete message from buffer
    bool parse_message();
    
    // Validate message
    bool validate_message(const protocol::MessageHeader* header, size_t msg_size);
    
    // Process parsed message
    void process_message(protocol::MessageType type, const uint8_t* msg_data);
    
    // Buffer for incomplete messages (TCP stream reassembly)
    static constexpr size_t BUFFER_SIZE = 8192;
    uint8_t buffer_[BUFFER_SIZE];
    size_t buffer_used_;
    
    // Callbacks
    TradeCallback trade_callback_;
    QuoteCallback quote_callback_;
    HeartbeatCallback heartbeat_callback_;
    
    // Sequence number tracking
    uint32_t last_sequence_number_;
    bool first_message_;
    
    // Configuration
    bool validate_checksum_;
    bool validate_sequence_;
    
    // Statistics
    ParserStats stats_;
};

} // namespace parser