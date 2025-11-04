#ifndef MATCHING_ENGINE_ORDER_MANAGER_H
#define MATCHING_ENGINE_ORDER_MANAGER_H

#include "../../common/protocol.h"
#include "matchengine.h"
#include <memory>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <string>
#include <vector>

namespace matching {
namespace engine {

// =============================================================================
// ORDER STATE
// =============================================================================
struct OrderState {
    uint64_t client_order_id;
    uint64_t exchange_order_id;
    uint64_t user_id;
    std::string symbol;
    protocol::Side side;
    protocol::OrderType order_type;
    uint64_t price;
    uint64_t original_quantity;
    uint64_t remaining_quantity;
    uint64_t filled_quantity;
    uint64_t timestamp;
    
    enum class Status {
        PENDING,
        ACTIVE,
        PARTIALLY_FILLED,
        FILLED,
        CANCELLED,
        REJECTED
    };
    
    Status status;
    
    OrderState()
        : client_order_id(0)
        , exchange_order_id(0)
        , user_id(0)
        , side(protocol::Side::BUY)
        , order_type(protocol::OrderType::LIMIT)
        , price(0)
        , original_quantity(0)
        , remaining_quantity(0)
        , filled_quantity(0)
        , timestamp(0)
        , status(Status::PENDING)
    {}
};

// =============================================================================
// OUTGOING MESSAGE CALLBACK
// =============================================================================
using MessageCallback = std::function<void(const void* data, size_t size)>;

// =============================================================================
// ORDER MANAGER
// =============================================================================
class OrderManager {
public:
    OrderManager();
    ~OrderManager();
    
    void set_message_callback(MessageCallback callback);
    bool add_symbol(const std::string& symbol);
    bool remove_symbol(const std::string& symbol);
    
    void handle_new_order(const protocol::NewOrderMessage& msg);
    void handle_cancel_order(const protocol::CancelOrderMessage& msg);
    
    struct Statistics {
        uint64_t total_orders_received;
        uint64_t total_orders_accepted;
        uint64_t total_orders_rejected;
        uint64_t total_orders_cancelled;
        uint64_t total_executions;
        uint64_t total_volume;
        
        Statistics()
            : total_orders_received(0)
            , total_orders_accepted(0)
            , total_orders_rejected(0)
            , total_orders_cancelled(0)
            , total_executions(0)
            , total_volume(0)
        {}
    };
    
    Statistics get_statistics() const { return stats_; }
    const OrderState* get_order(uint64_t client_order_id) const;
    std::vector<const OrderState*> get_user_orders(uint64_t user_id) const;
    
private:
    // -------------------------------------------------------------------------
    // INTERNAL ORDER BOOK MANAGEMENT
    // -------------------------------------------------------------------------
    
    struct SymbolData {
        mx_order_book_t* book;      // me_lib order book for this symbol
        uint64_t last_trade_id;
        
        SymbolData() : book(nullptr), last_trade_id(0) {}
        ~SymbolData() {
            if (book) {
                mx_order_book_free(book);
            }
        }
    };
    
    // Context for me_lib
    mx_context_t* context_;
    
    // Symbol -> OrderBook mapping
    std::unordered_map<std::string, std::unique_ptr<SymbolData>> books_;
    
    // -------------------------------------------------------------------------
    // ORDER STATE TRACKING
    // -------------------------------------------------------------------------
    
    std::unordered_map<uint64_t, OrderState> orders_;
    std::unordered_map<uint64_t, uint64_t> exchange_to_client_;
    std::unordered_map<uint64_t, std::vector<uint64_t>> user_orders_;
    
    // -------------------------------------------------------------------------
    // ID GENERATION
    // -------------------------------------------------------------------------
    
    uint64_t next_exchange_order_id_;
    uint64_t next_execution_id_;
    uint64_t next_sequence_;
    
    uint64_t generate_exchange_order_id() { return next_exchange_order_id_++; }
    uint64_t generate_execution_id() { return next_execution_id_++; }
    uint64_t generate_sequence() { return next_sequence_++; }
    
    // -------------------------------------------------------------------------
    // MESSAGE SENDING
    // -------------------------------------------------------------------------
    
    MessageCallback message_callback_;
    
    void send_message(const void* data, size_t size);
    void send_order_ack(const OrderState& order);
    void send_order_reject(uint64_t client_order_id, uint64_t user_id, 
                          protocol::RejectReason reason, const std::string& text);
    void send_execution(const OrderState& order, uint64_t fill_price, 
                       uint64_t fill_quantity, uint64_t execution_id);
    void send_cancel_ack(const OrderState& order);
    void send_trade(const std::string& symbol, uint64_t trade_id,
                   uint64_t price, uint64_t quantity);
    void send_quote(const std::string& symbol, uint32_t bid_price, 
                   uint32_t bid_quantity, uint32_t ask_price, uint32_t ask_quantity);
    
    // -------------------------------------------------------------------------
    // ORDER VALIDATION
    // -------------------------------------------------------------------------
    
    protocol::RejectReason validate_new_order(const protocol::NewOrderMessage& msg);
    
    // -------------------------------------------------------------------------
    // MATCHING ENGINE CALLBACKS
    // -------------------------------------------------------------------------
    
    static void trade_callback(void* user_data, 
                              uint64_t aggressive_order_id,
                              uint64_t passive_order_id,
                              uint32_t price,
                              uint32_t quantity,
                              uint64_t timestamp);
    
    static void order_callback(void* user_data,
                              uint64_t order_id,
                              mx_order_event_t event,
                              uint32_t filled_quantity,
                              uint32_t remaining_quantity);
    
    void on_trade(uint64_t aggressive_order_id,
                 uint64_t passive_order_id,
                 uint32_t price,
                 uint32_t quantity,
                 uint64_t timestamp);
    
    void on_order_event(uint64_t order_id,
                       mx_order_event_t event,
                       uint32_t filled_quantity,
                       uint32_t remaining_quantity);
    
    // -------------------------------------------------------------------------
    // INTERNAL STATE MANAGEMENT
    // -------------------------------------------------------------------------
    
    void update_order_filled(OrderState& order, uint64_t filled_qty);
    void update_order_cancelled(OrderState& order);
    
    uint64_t get_timestamp() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
    
    // -------------------------------------------------------------------------
    // STATISTICS
    // -------------------------------------------------------------------------
    
    Statistics stats_;
};

} // namespace engine
} // namespace matching

#endif // MATCHING_ENGINE_ORDER_MANAGER_H
