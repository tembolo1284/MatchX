#ifndef MATCHING_ENGINE_PROTOCOL_H
#define MATCHING_ENGINE_PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <string>

namespace matching {
namespace protocol {

// =============================================================================
// PROTOCOL VERSION
// =============================================================================
constexpr uint8_t PROTOCOL_VERSION = 1;

// =============================================================================
// MESSAGE TYPES
// =============================================================================
enum class MessageType : uint8_t {
    // Client → Engine (Orders)
    NEW_ORDER           = 0x01,
    CANCEL_ORDER        = 0x02,
    REPLACE_ORDER       = 0x03,
    
    // Engine → Client (Responses)
    ORDER_ACK           = 0x10,
    ORDER_REJECT        = 0x11,
    ORDER_CANCELLED     = 0x12,
    ORDER_REPLACED      = 0x13,
    
    // Engine → Client (Executions)
    EXECUTION           = 0x20,
    
    // Market Data
    TRADE               = 0x30,
    QUOTE               = 0x31,
    
    // System
    HEARTBEAT           = 0xF0,
    LOGON               = 0xF1,
    LOGOUT              = 0xF2,
};

// =============================================================================
// ORDER SIDE
// =============================================================================
enum class Side : uint8_t {
    BUY  = 0x01,
    SELL = 0x02,
};

// =============================================================================
// ORDER TYPE
// =============================================================================
enum class OrderType : uint8_t {
    LIMIT  = 0x01,
    MARKET = 0x02,
};

// =============================================================================
// REJECT REASONS
// =============================================================================
enum class RejectReason : uint8_t {
    NONE                    = 0x00,
    INVALID_SYMBOL          = 0x01,
    INVALID_PRICE           = 0x02,
    INVALID_QUANTITY        = 0x03,
    DUPLICATE_ORDER_ID      = 0x04,
    UNKNOWN_ORDER           = 0x05,
    INSUFFICIENT_FUNDS      = 0x06,
    MARKET_CLOSED           = 0x07,
    SYSTEM_ERROR            = 0x08,
};

// =============================================================================
// MESSAGE HEADER (Fixed size: 16 bytes)
// =============================================================================
// Every message starts with this header for fast parsing
struct MessageHeader {
    uint8_t  version;       // Protocol version
    uint8_t  type;          // MessageType
    uint16_t reserved;      // For alignment and future use
    uint32_t length;        // Total message length (including header)
    uint64_t sequence;      // Sequence number for ordering/gap detection
    
    MessageHeader() 
        : version(PROTOCOL_VERSION)
        , type(0)
        , reserved(0)
        , length(sizeof(MessageHeader))
        , sequence(0)
    {}
    
    MessageType get_type() const { 
        return static_cast<MessageType>(type); 
    }
    
    void set_type(MessageType t) { 
        type = static_cast<uint8_t>(t); 
    }
} __attribute__((packed));

static_assert(sizeof(MessageHeader) == 16, "MessageHeader must be 16 bytes");

// =============================================================================
// NEW ORDER MESSAGE
// =============================================================================
struct NewOrderMessage {
    MessageHeader header;
    
    char     symbol[16];        // Symbol (null-terminated, padded)
    uint64_t client_order_id;   // Client's unique order ID
    uint64_t user_id;           // User/account identifier
    uint8_t  side;              // Side enum
    uint8_t  order_type;        // OrderType enum
    uint16_t reserved;          // Alignment
    uint64_t price;             // Price in fixed-point (e.g., cents)
    uint64_t quantity;          // Quantity
    uint64_t timestamp;         // Client timestamp (nanoseconds since epoch)
    
    NewOrderMessage() 
        : header()
        , client_order_id(0)
        , user_id(0)
        , side(0)
        , order_type(0)
        , reserved(0)
        , price(0)
        , quantity(0)
        , timestamp(0)
    {
        memset(symbol, 0, sizeof(symbol));
        header.set_type(MessageType::NEW_ORDER);
        header.length = sizeof(NewOrderMessage);
    }
    
    void set_symbol(const std::string& sym) {
        strncpy(symbol, sym.c_str(), sizeof(symbol) - 1);
        symbol[sizeof(symbol) - 1] = '\0';
    }
    
    std::string get_symbol() const {
        return std::string(symbol, strnlen(symbol, sizeof(symbol)));
    }
    
    Side get_side() const { 
        return static_cast<Side>(side); 
    }
    
    OrderType get_order_type() const { 
        return static_cast<OrderType>(order_type); 
    }
} __attribute__((packed));

// =============================================================================
// CANCEL ORDER MESSAGE
// =============================================================================
struct CancelOrderMessage {
    MessageHeader header;
    
    char     symbol[16];        // Symbol
    uint64_t client_order_id;   // Original order ID to cancel
    uint64_t user_id;           // User identifier
    uint64_t timestamp;         // Client timestamp
    
    CancelOrderMessage()
        : header()
        , client_order_id(0)
        , user_id(0)
        , timestamp(0)
    {
        memset(symbol, 0, sizeof(symbol));
        header.set_type(MessageType::CANCEL_ORDER);
        header.length = sizeof(CancelOrderMessage);
    }
    
    void set_symbol(const std::string& sym) {
        strncpy(symbol, sym.c_str(), sizeof(symbol) - 1);
        symbol[sizeof(symbol) - 1] = '\0';
    }
    
    std::string get_symbol() const {
        return std::string(symbol, strnlen(symbol, sizeof(symbol)));
    }
} __attribute__((packed));

// =============================================================================
// ORDER ACKNOWLEDGEMENT
// =============================================================================
struct OrderAckMessage {
    MessageHeader header;
    
    uint64_t client_order_id;   // Client's order ID
    uint64_t exchange_order_id; // Exchange-assigned order ID
    uint64_t user_id;
    uint64_t timestamp;         // Exchange timestamp
    
    OrderAckMessage()
        : header()
        , client_order_id(0)
        , exchange_order_id(0)
        , user_id(0)
        , timestamp(0)
    {
        header.set_type(MessageType::ORDER_ACK);
        header.length = sizeof(OrderAckMessage);
    }
} __attribute__((packed));

// =============================================================================
// ORDER REJECT
// =============================================================================
struct OrderRejectMessage {
    MessageHeader header;
    
    uint64_t client_order_id;
    uint64_t user_id;
    uint8_t  reason;            // RejectReason enum
    uint8_t  reserved[7];       // Alignment
    char     text[64];          // Human-readable reason
    uint64_t timestamp;
    
    OrderRejectMessage()
        : header()
        , client_order_id(0)
        , user_id(0)
        , reason(0)
        , reserved{0}
        , timestamp(0)
    {
        memset(text, 0, sizeof(text));
        header.set_type(MessageType::ORDER_REJECT);
        header.length = sizeof(OrderRejectMessage);
    }
    
    RejectReason get_reason() const {
        return static_cast<RejectReason>(reason);
    }
    
    void set_text(const std::string& txt) {
        strncpy(text, txt.c_str(), sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
    }
} __attribute__((packed));

// =============================================================================
// EXECUTION REPORT (Trade Fill)
// =============================================================================
struct ExecutionMessage {
    MessageHeader header;
    
    char     symbol[16];
    uint64_t client_order_id;
    uint64_t exchange_order_id;
    uint64_t execution_id;      // Unique execution ID
    uint64_t user_id;
    uint8_t  side;
    uint8_t  reserved[7];
    uint64_t fill_price;        // Execution price
    uint64_t fill_quantity;     // Executed quantity
    uint64_t leaves_quantity;   // Remaining quantity
    uint64_t timestamp;
    
    ExecutionMessage()
        : header()
        , client_order_id(0)
        , exchange_order_id(0)
        , execution_id(0)
        , user_id(0)
        , side(0)
        , reserved{0}
        , fill_price(0)
        , fill_quantity(0)
        , leaves_quantity(0)
        , timestamp(0)
    {
        memset(symbol, 0, sizeof(symbol));
        header.set_type(MessageType::EXECUTION);
        header.length = sizeof(ExecutionMessage);
    }
    
    void set_symbol(const std::string& sym) {
        strncpy(symbol, sym.c_str(), sizeof(symbol) - 1);
        symbol[sizeof(symbol) - 1] = '\0';
    }
    
    std::string get_symbol() const {
        return std::string(symbol, strnlen(symbol, sizeof(symbol)));
    }
    
    Side get_side() const { 
        return static_cast<Side>(side); 
    }
} __attribute__((packed));

// =============================================================================
// TRADE (Market Data)
// =============================================================================
struct TradeMessage {
    MessageHeader header;
    
    char     symbol[16];
    uint64_t trade_id;
    uint64_t price;
    uint64_t quantity;
    uint64_t timestamp;
    
    TradeMessage()
        : header()
        , trade_id(0)
        , price(0)
        , quantity(0)
        , timestamp(0)
    {
        memset(symbol, 0, sizeof(symbol));
        header.set_type(MessageType::TRADE);
        header.length = sizeof(TradeMessage);
    }
    
    void set_symbol(const std::string& sym) {
        strncpy(symbol, sym.c_str(), sizeof(symbol) - 1);
        symbol[sizeof(symbol) - 1] = '\0';
    }
} __attribute__((packed));

// =============================================================================
// QUOTE (Best Bid/Ask)
// =============================================================================
struct QuoteMessage {
    MessageHeader header;
    
    char     symbol[16];
    uint64_t bid_price;
    uint64_t bid_quantity;
    uint64_t ask_price;
    uint64_t ask_quantity;
    uint64_t timestamp;
    
    QuoteMessage()
        : header()
        , bid_price(0)
        , bid_quantity(0)
        , ask_price(0)
        , ask_quantity(0)
        , timestamp(0)
    {
        memset(symbol, 0, sizeof(symbol));
        header.set_type(MessageType::QUOTE);
        header.length = sizeof(QuoteMessage);
    }
    
    void set_symbol(const std::string& sym) {
        strncpy(symbol, sym.c_str(), sizeof(symbol) - 1);
        symbol[sizeof(symbol) - 1] = '\0';
    }
} __attribute__((packed));

// =============================================================================
// HEARTBEAT (Keep-alive)
// =============================================================================
struct HeartbeatMessage {
    MessageHeader header;
    uint64_t timestamp;
    
    HeartbeatMessage()
        : header()
        , timestamp(0)
    {
        header.set_type(MessageType::HEARTBEAT);
        header.length = sizeof(HeartbeatMessage);
    }
} __attribute__((packed));

} // namespace protocol
} // namespace matching

#endif // MATCHING_ENGINE_PROTOCOL_H
