#include "parser.h"
#include <iostream>
#include <iomanip>
#include <vector>

using namespace parser;
using namespace protocol;

// Helper to create a trade message
std::vector<uint8_t> create_trade_message(uint32_t seq, uint16_t symbol, 
                                           double price, uint32_t qty) {
    TradeMessage msg;
    msg.header.msg_type = MessageType::TRADE;
    msg.header.sequence_number = seq;
    msg.header.timestamp_ns = get_timestamp_ns();
    msg.header.symbol_id = symbol;
    msg.payload.price = price;
    msg.payload.quantity = qty;
    
    // Calculate checksum
    msg.checksum = calculate_checksum(&msg, sizeof(msg.header) + sizeof(msg.payload));
    
    // Convert to byte array
    std::vector<uint8_t> bytes(TradeMessage::SIZE);
    memcpy(bytes.data(), &msg, TradeMessage::SIZE);
    
    return bytes;
}

// Helper to create a quote message
std::vector<uint8_t> create_quote_message(uint32_t seq, uint16_t symbol,
                                          double bid, uint32_t bid_qty,
                                          double ask, uint32_t ask_qty) {
    QuoteMessage msg;
    msg.header.msg_type = MessageType::QUOTE;
    msg.header.sequence_number = seq;
    msg.header.timestamp_ns = get_timestamp_ns();
    msg.header.symbol_id = symbol;
    msg.payload.bid_price = bid;
    msg.payload.bid_quantity = bid_qty;
    msg.payload.ask_price = ask;
    msg.payload.ask_quantity = ask_qty;
    
    msg.checksum = calculate_checksum(&msg, sizeof(msg.header) + sizeof(msg.payload));
    
    std::vector<uint8_t> bytes(QuoteMessage::SIZE);
    memcpy(bytes.data(), &msg, QuoteMessage::SIZE);
    
    return bytes;
}

// Test 1: Parse single complete message
bool test_single_message() {
    std::cout << "\n=== Test 1: Single Complete Message ===\n";
    
    MessageParser parser;
    
    int trade_count = 0;
    parser.set_trade_callback([&](const TradeMessage& msg) {
        std::cout << "Parsed TRADE: Seq=" << msg.header.sequence_number
                  << " Symbol=" << msg.header.symbol_id
                  << " Price=" << msg.payload.price
                  << " Qty=" << msg.payload.quantity << "\n";
        trade_count++;
    });
    
    auto bytes = create_trade_message(1, 42, 1234.56, 1000);
    size_t consumed = parser.parse(bytes.data(), bytes.size());
    
    if (consumed != bytes.size()) {
        std::cerr << "FAIL: Not all bytes consumed\n";
        return false;
    }
    
    if (trade_count != 1) {
        std::cerr << "FAIL: Expected 1 trade, got " << trade_count << "\n";
        return false;
    }
    
    std::cout << "PASS: Single message parsed correctly\n";
    return true;
}

// Test 2: Parse multiple messages in one buffer
bool test_multiple_messages() {
    std::cout << "\n=== Test 2: Multiple Messages in Buffer ===\n";
    
    MessageParser parser;
    
    int message_count = 0;
    parser.set_trade_callback([&](const TradeMessage&) { message_count++; });
    parser.set_quote_callback([&](const QuoteMessage&) { message_count++; });
    
    // Create buffer with 3 messages
    std::vector<uint8_t> buffer;
    
    auto msg1 = create_trade_message(1, 10, 100.0, 500);
    auto msg2 = create_quote_message(2, 20, 200.0, 100, 201.0, 150);
    auto msg3 = create_trade_message(3, 30, 300.0, 750);
    
    buffer.insert(buffer.end(), msg1.begin(), msg1.end());
    buffer.insert(buffer.end(), msg2.begin(), msg2.end());
    buffer.insert(buffer.end(), msg3.begin(), msg3.end());
    
    size_t consumed = parser.parse(buffer.data(), buffer.size());
    
    if (message_count != 3) {
        std::cerr << "FAIL: Expected 3 messages, got " << message_count << "\n";
        return false;
    }
    
    std::cout << "PASS: Multiple messages parsed correctly\n";
    return true;
}

// Test 3: Parse fragmented message (TCP stream simulation)
bool test_fragmented_message() {
    std::cout << "\n=== Test 3: Fragmented Message (TCP Stream) ===\n";
    
    MessageParser parser;
    
    int trade_count = 0;
    parser.set_trade_callback([&](const TradeMessage& msg) {
        std::cout << "Parsed fragmented TRADE: Seq=" << msg.header.sequence_number << "\n";
        trade_count++;
    });
    
    auto bytes = create_trade_message(1, 42, 1234.56, 1000);
    
    // Split message into 3 fragments
    size_t frag1_size = 10;  // Partial header
    size_t frag2_size = 15;  // Rest of header + some payload
    size_t frag3_size = bytes.size() - frag1_size - frag2_size;  // Remaining
    
    std::cout << "Sending fragment 1 (" << frag1_size << " bytes)...\n";
    parser.parse(bytes.data(), frag1_size);
    
    std::cout << "Sending fragment 2 (" << frag2_size << " bytes)...\n";
    parser.parse(bytes.data() + frag1_size, frag2_size);
    
    std::cout << "Sending fragment 3 (" << frag3_size << " bytes)...\n";
    parser.parse(bytes.data() + frag1_size + frag2_size, frag3_size);
    
    if (trade_count != 1) {
        std::cerr << "FAIL: Expected 1 trade after reassembly, got " << trade_count << "\n";
        return false;
    }
    
    std::cout << "PASS: Fragmented message reassembled correctly\n";
    return true;
}

// Test 4: Sequence gap detection
bool test_sequence_gaps() {
    std::cout << "\n=== Test 4: Sequence Gap Detection ===\n";
    
    MessageParser parser;
    
    parser.set_trade_callback([](const TradeMessage&) {});
    
    // Send messages with sequence: 1, 2, 5 (gap of 2 messages)
    auto msg1 = create_trade_message(1, 10, 100.0, 500);
    auto msg2 = create_trade_message(2, 10, 101.0, 500);
    auto msg3 = create_trade_message(5, 10, 102.0, 500);  // Gap!
    
    parser.parse(msg1.data(), msg1.size());
    parser.parse(msg2.data(), msg2.size());
    parser.parse(msg3.data(), msg3.size());
    
    const auto& stats = parser.get_stats();
    
    if (stats.sequence_gaps != 1) {
        std::cerr << "FAIL: Expected 1 sequence gap, got " << stats.sequence_gaps << "\n";
        return false;
    }
    
    std::cout << "PASS: Sequence gap detected correctly\n";
    return true;
}

// Test 5: Checksum validation
bool test_checksum_validation() {
    std::cout << "\n=== Test 5: Checksum Validation ===\n";
    
    MessageParser parser;
    
    int trade_count = 0;
    parser.set_trade_callback([&](const TradeMessage&) { trade_count++; });
    
    auto bytes = create_trade_message(1, 42, 1234.56, 1000);
    
    // Corrupt the checksum
    bytes[bytes.size() - 1] ^= 0xFF;
    
    parser.parse(bytes.data(), bytes.size());
    
    const auto& stats = parser.get_stats();
    
    if (stats.checksum_errors != 1) {
        std::cerr << "FAIL: Expected 1 checksum error, got " << stats.checksum_errors << "\n";
        return false;
    }
    
    if (trade_count != 0) {
        std::cerr << "FAIL: Corrupted message should not be processed\n";
        return false;
    }
    
    std::cout << "PASS: Checksum validation working\n";
    return true;
}

// Test 6: Parser statistics
bool test_statistics() {
    std::cout << "\n=== Test 6: Parser Statistics ===\n";
    
    MessageParser parser;
    
    parser.set_trade_callback([](const TradeMessage&) {});
    parser.set_quote_callback([](const QuoteMessage&) {});
    
    // Send 10 trades and 15 quotes
    for (int i = 1; i <= 10; ++i) {
        auto msg = create_trade_message(i, 10, 100.0 + i, 500);
        parser.parse(msg.data(), msg.size());
    }
    
    for (int i = 11; i <= 25; ++i) {
        auto msg = create_quote_message(i, 20, 200.0, 100, 201.0, 150);
        parser.parse(msg.data(), msg.size());
    }
    
    const auto& stats = parser.get_stats();
    
    std::cout << "Messages parsed: " << stats.messages_parsed << "\n";
    std::cout << "Trades: " << stats.trades_parsed << "\n";
    std::cout << "Quotes: " << stats.quotes_parsed << "\n";
    
    if (stats.messages_parsed != 25 || stats.trades_parsed != 10 || stats.quotes_parsed != 15) {
        std::cerr << "FAIL: Statistics incorrect\n";
        return false;
    }
    
    std::cout << "PASS: Statistics tracking correctly\n";
    return true;
}

int main() {
    std::cout << "========================================\n";
    std::cout << "     Binary Protocol Parser Tests\n";
    std::cout << "========================================\n";
    
    int passed = 0;
    int total = 6;
    
    if (test_single_message()) passed++;
    if (test_multiple_messages()) passed++;
    if (test_fragmented_message()) passed++;
    if (test_sequence_gaps()) passed++;
    if (test_checksum_validation()) passed++;
    if (test_statistics()) passed++;
    
    std::cout << "\n========================================\n";
    std::cout << "Results: " << passed << "/" << total << " tests passed\n";
    std::cout << "========================================\n";
    
    return (passed == total) ? 0 : 1;
}