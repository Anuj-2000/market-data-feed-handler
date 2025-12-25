#include "parser.h"
#include <iostream>
#include <algorithm>

namespace parser {

MessageParser::MessageParser()
    : buffer_used_(0),
      last_sequence_number_(0),
      first_message_(true),
      validate_checksum_(true),
      validate_sequence_(true),
      stats_() {
    memset(buffer_, 0, BUFFER_SIZE);
}

MessageParser::~MessageParser() = default;

void MessageParser::set_trade_callback(TradeCallback callback) {
    trade_callback_ = callback;
}

void MessageParser::set_quote_callback(QuoteCallback callback) {
    quote_callback_ = callback;
}

void MessageParser::set_heartbeat_callback(HeartbeatCallback callback) {
    heartbeat_callback_ = callback;
}

size_t MessageParser::parse(const uint8_t* data, size_t len) {
    if (len == 0) return 0;
    
    // Copy new data to buffer
    size_t bytes_to_copy = std::min(len, BUFFER_SIZE - buffer_used_);
    if (bytes_to_copy == 0) {
        // Buffer full - this shouldn't happen with proper buffer sizing
        std::cerr << "Parser buffer overflow, resetting\n";
        reset();
        return 0;
    }
    
    memcpy(buffer_ + buffer_used_, data, bytes_to_copy);
    buffer_used_ += bytes_to_copy;
    
    // Parse all complete messages in buffer
    while (parse_message()) {
        // Keep parsing until buffer is exhausted
    }
    
    return bytes_to_copy;
}

bool MessageParser::parse_message() {
    // Need at least header to determine message type
    if (buffer_used_ < sizeof(protocol::MessageHeader)) {
        return false;  // Not enough data yet
    }
    
    // Read header
    const protocol::MessageHeader* header = 
        reinterpret_cast<const protocol::MessageHeader*>(buffer_);
    
    // Get expected message size
    size_t msg_size = protocol::get_message_size(header->msg_type);
    
    if (msg_size == 0) {
        // Invalid message type
        std::cerr << "Invalid message type: " 
                  << static_cast<uint16_t>(header->msg_type) << "\n";
        stats_.malformed_messages++;
        
        // Skip header and try to resync
        buffer_used_ -= sizeof(protocol::MessageHeader);
        memmove(buffer_, buffer_ + sizeof(protocol::MessageHeader), buffer_used_);
        return false;
    }
    
    // Check if we have complete message
    if (buffer_used_ < msg_size) {
        return false;  // Wait for more data
    }
    
    // Validate message
    if (!validate_message(header, msg_size)) {
        // Skip this message and continue
        buffer_used_ -= msg_size;
        memmove(buffer_, buffer_ + msg_size, buffer_used_);
        return true;  // Try next message
    }
    
    // Process message
    process_message(header->msg_type, buffer_);
    
    // Remove processed message from buffer
    buffer_used_ -= msg_size;
    if (buffer_used_ > 0) {
        memmove(buffer_, buffer_ + msg_size, buffer_used_);
    }
    
    return true;  // Successfully parsed one message
}

bool MessageParser::validate_message(const protocol::MessageHeader* header, 
                                      size_t msg_size) {
    // Validate checksum if enabled
    if (validate_checksum_) {
        if (!protocol::verify_checksum(buffer_, msg_size)) {
            std::cerr << "Checksum validation failed for seq=" 
                      << header->sequence_number << "\n";
            stats_.checksum_errors++;
            return false;
        }
    }
    
    // Validate sequence number if enabled
    if (validate_sequence_ && !first_message_) {
        if (header->sequence_number != last_sequence_number_ + 1) {
            // Sequence gap detected
            uint32_t gap = header->sequence_number - last_sequence_number_ - 1;
            std::cerr << "Sequence gap detected: expected " 
                      << (last_sequence_number_ + 1)
                      << ", got " << header->sequence_number
                      << " (gap of " << gap << " messages)\n";
            stats_.sequence_gaps++;
            // Continue processing despite gap
        }
    }
    
    last_sequence_number_ = header->sequence_number;
    first_message_ = false;
    
    return true;
}

void MessageParser::process_message(protocol::MessageType type, 
                                     const uint8_t* msg_data) {
    stats_.messages_parsed++;
    
    switch (type) {
        case protocol::MessageType::TRADE: {
            stats_.trades_parsed++;
            if (trade_callback_) {
                const protocol::TradeMessage* msg = 
                    reinterpret_cast<const protocol::TradeMessage*>(msg_data);
                trade_callback_(*msg);
            }
            break;
        }
        
        case protocol::MessageType::QUOTE: {
            stats_.quotes_parsed++;
            if (quote_callback_) {
                const protocol::QuoteMessage* msg = 
                    reinterpret_cast<const protocol::QuoteMessage*>(msg_data);
                quote_callback_(*msg);
            }
            break;
        }
        
        case protocol::MessageType::HEARTBEAT: {
            stats_.heartbeats_parsed++;
            if (heartbeat_callback_) {
                const protocol::HeartbeatMessage* msg = 
                    reinterpret_cast<const protocol::HeartbeatMessage*>(msg_data);
                heartbeat_callback_(*msg);
            }
            break;
        }
        
        default:
            stats_.malformed_messages++;
            std::cerr << "Unknown message type: " 
                      << static_cast<uint16_t>(type) << "\n";
            break;
    }
}

void MessageParser::reset() {
    buffer_used_ = 0;
    last_sequence_number_ = 0;
    first_message_ = true;
    memset(buffer_, 0, BUFFER_SIZE);
}

} // namespace parser