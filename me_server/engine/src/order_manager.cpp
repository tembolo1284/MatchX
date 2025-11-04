#include "order_manager.h"
#include <cstring>
#include <iostream>

namespace matching {
namespace engine {

// =============================================================================
// CONSTRUCTOR / DESTRUCTOR
// =============================================================================

OrderManager::OrderManager()
    : context_(nullptr)
    , next_exchange_order_id_(1)
    , next_execution_id_(1)
    , next_sequence_(1)
    , message_callback_(nullptr)
{
    // Create me_lib context
    context_ = mx_context_new();
    if (!context_) {
        std::cerr << "[OrderManager] Failed to create mx_context" << std::endl;
        return;
    }
    
    // Set callbacks
    mx_context_set_callbacks(context_, 
                            &OrderManager::trade_callback,
                            &OrderManager::order_callback,
                            this);
}

OrderManager::~OrderManager() {
    if (context_) {
        mx_context_free(context_);
    }
}

// =============================================================================
// INITIALIZATION
// =============================================================================

void OrderManager::set_message_callback(MessageCallback callback) {
    message_callback_ = callback;
}

bool OrderManager::add_symbol(const std::string& symbol) {
    if (books_.find(symbol) != books_.end()) {
        return false; // Symbol already exists
    }
    
    auto data = std::make_unique<SymbolData>();
    
    // Create order book for this symbol
    data->book = mx_order_book_new(context_, symbol.c_str());
    if (!data->book) {
        std::cerr << "[OrderManager] Failed to create order book for symbol: " << symbol << std::endl;
        return false;
    }
    
    data->last_trade_id = 0;
    books_[symbol] = std::move(data);
    
    std::cout << "[OrderManager] Added symbol: " << symbol << std::endl;
    return true;
}

bool OrderManager::remove_symbol(const std::string& symbol) {
    auto it = books_.find(symbol);
    if (it == books_.end()) {
        return false;
    }
    
    books_.erase(it);
    std::cout << "[OrderManager] Removed symbol: " << symbol << std::endl;
    return true;
}

// =============================================================================
// ORDER OPERATIONS
// =============================================================================

void OrderManager::handle_new_order(const protocol::NewOrderMessage& msg) {
    stats_.total_orders_received++;
    
    // Validate the order
    protocol::RejectReason reject_reason = validate_new_order(msg);
    if (reject_reason != protocol::RejectReason::NONE) {
        stats_.total_orders_rejected++;
        send_order_reject(msg.client_order_id, msg.user_id, reject_reason, 
                         "Order validation failed");
        return;
    }
    
    // Check for duplicate order ID
    if (orders_.find(msg.client_order_id) != orders_.end()) {
        stats_.total_orders_rejected++;
        send_order_reject(msg.client_order_id, msg.user_id, 
                         protocol::RejectReason::DUPLICATE_ORDER_ID,
                         "Order ID already exists");
        return;
    }
    
    // Get the order book for this symbol
    std::string symbol = msg.get_symbol();
    auto book_it = books_.find(symbol);
    if (book_it == books_.end()) {
        stats_.total_orders_rejected++;
        send_order_reject(msg.client_order_id, msg.user_id,
                         protocol::RejectReason::INVALID_SYMBOL,
                         "Symbol not found");
        return;
    }
    
    // Create order state
    OrderState order;
    order.client_order_id = msg.client_order_id;
    order.exchange_order_id = generate_exchange_order_id();
    order.user_id = msg.user_id;
    order.symbol = symbol;
    order.side = msg.get_side();
    order.order_type = msg.get_order_type();
    order.price = msg.price;
    order.original_quantity = msg.quantity;
    order.remaining_quantity = msg.quantity;
    order.filled_quantity = 0;
    order.timestamp = get_timestamp();
    order.status = OrderState::Status::PENDING;
    
    // Store order state
    orders_[order.client_order_id] = order;
    exchange_to_client_[order.exchange_order_id] = order.client_order_id;
    user_orders_[order.user_id].push_back(order.client_order_id);
    
    // Send acknowledgement
    send_order_ack(order);
    
    // Update status to ACTIVE
    orders_[order.client_order_id].status = OrderState::Status::ACTIVE;
    
    stats_.total_orders_accepted++;
    
    // Submit to matching engine
    mx_side_t mx_side = (order.side == protocol::Side::BUY) ? MX_SIDE_BUY : MX_SIDE_SELL;
    
    int result = mx_order_book_add_limit(
        book_it->second->book,
        order.exchange_order_id,
        mx_side,
        static_cast<uint32_t>(order.price),
        static_cast<uint32_t>(order.remaining_quantity)
    );
    
    if (result != MX_STATUS_OK) {
        std::cerr << "[OrderManager] Failed to add order to me_lib: " 
                  << mx_status_message(static_cast<mx_status_t>(result)) << std::endl;
    }
    
    // Get best bid/ask for quote
    uint32_t best_bid = mx_order_book_get_best_bid(book_it->second->book);
    uint32_t best_ask = mx_order_book_get_best_ask(book_it->second->book);
    
    // Get volumes (simplified - you might want depth here)
    uint32_t bid_volume = best_bid ? mx_order_book_get_volume_at_price(book_it->second->book, MX_SIDE_BUY, best_bid) : 0;
    uint32_t ask_volume = best_ask ? mx_order_book_get_volume_at_price(book_it->second->book, MX_SIDE_SELL, best_ask) : 0;
    
    send_quote(symbol, best_bid, bid_volume, best_ask, ask_volume);
}

void OrderManager::handle_cancel_order(const protocol::CancelOrderMessage& msg) {
    // Find the order
    auto order_it = orders_.find(msg.client_order_id);
    if (order_it == orders_.end()) {
        send_order_reject(msg.client_order_id, msg.user_id,
                         protocol::RejectReason::UNKNOWN_ORDER,
                         "Order not found");
        return;
    }
    
    OrderState& order = order_it->second;
    
    // Verify user owns this order
    if (order.user_id != msg.user_id) {
        send_order_reject(msg.client_order_id, msg.user_id,
                         protocol::RejectReason::UNKNOWN_ORDER,
                         "Order does not belong to user");
        return;
    }
    
    // Check if order can be cancelled
    if (order.status == OrderState::Status::FILLED ||
        order.status == OrderState::Status::CANCELLED ||
        order.status == OrderState::Status::REJECTED) {
        send_order_reject(msg.client_order_id, msg.user_id,
                         protocol::RejectReason::UNKNOWN_ORDER,
                         "Order cannot be cancelled");
        return;
    }
    
    // Find the order book
    auto book_it = books_.find(order.symbol);
    if (book_it == books_.end()) {
        send_order_reject(msg.client_order_id, msg.user_id,
                         protocol::RejectReason::SYSTEM_ERROR,
                         "Order book not found");
        return;
    }
    
    // Cancel in the matching engine
    int result = mx_order_book_cancel(book_it->second->book, order.exchange_order_id);
    
    if (result == MX_STATUS_OK) {
        update_order_cancelled(order);
        send_cancel_ack(order);
        stats_.total_orders_cancelled++;
        
        // Send updated quote
        uint32_t best_bid = mx_order_book_get_best_bid(book_it->second->book);
        uint32_t best_ask = mx_order_book_get_best_ask(book_it->second->book);
        uint32_t bid_volume = best_bid ? mx_order_book_get_volume_at_price(book_it->second->book, MX_SIDE_BUY, best_bid) : 0;
        uint32_t ask_volume = best_ask ? mx_order_book_get_volume_at_price(book_it->second->book, MX_SIDE_SELL, best_ask) : 0;
        
        send_quote(order.symbol, best_bid, bid_volume, best_ask, ask_volume);
    } else {
        send_order_reject(msg.client_order_id, msg.user_id,
                         protocol::RejectReason::UNKNOWN_ORDER,
                         "Order not found in book (may be filled)");
    }
}

// =============================================================================
// STATISTICS & MONITORING
// =============================================================================

const OrderState* OrderManager::get_order(uint64_t client_order_id) const {
    auto it = orders_.find(client_order_id);
    if (it == orders_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<const OrderState*> OrderManager::get_user_orders(uint64_t user_id) const {
    std::vector<const OrderState*> result;
    
    auto it = user_orders_.find(user_id);
    if (it == user_orders_.end()) {
        return result;
    }
    
    for (uint64_t client_order_id : it->second) {
        auto order_it = orders_.find(client_order_id);
        if (order_it != orders_.end()) {
            result.push_back(&order_it->second);
        }
    }
    
    return result;
}

// =============================================================================
// MESSAGE SENDING
// =============================================================================

void OrderManager::send_message(const void* data, size_t size) {
    if (message_callback_) {
        message_callback_(data, size);
    }
}

void OrderManager::send_order_ack(const OrderState& order) {
    protocol::OrderAckMessage msg;
    msg.header.sequence = generate_sequence();
    msg.client_order_id = order.client_order_id;
    msg.exchange_order_id = order.exchange_order_id;
    msg.user_id = order.user_id;
    msg.timestamp = get_timestamp();
    
    send_message(&msg, sizeof(msg));
}

void OrderManager::send_order_reject(uint64_t client_order_id, uint64_t user_id,
                                     protocol::RejectReason reason, const std::string& text) {
    protocol::OrderRejectMessage msg;
    msg.header.sequence = generate_sequence();
    msg.client_order_id = client_order_id;
    msg.user_id = user_id;
    msg.reason = static_cast<uint8_t>(reason);
    msg.set_text(text);
    msg.timestamp = get_timestamp();
    
    send_message(&msg, sizeof(msg));
}

void OrderManager::send_execution(const OrderState& order, uint64_t fill_price,
                                  uint64_t fill_quantity, uint64_t execution_id) {
    protocol::ExecutionMessage msg;
    msg.header.sequence = generate_sequence();
    msg.set_symbol(order.symbol);
    msg.client_order_id = order.client_order_id;
    msg.exchange_order_id = order.exchange_order_id;
    msg.execution_id = execution_id;
    msg.user_id = order.user_id;
    msg.side = static_cast<uint8_t>(order.side);
    msg.fill_price = fill_price;
    msg.fill_quantity = fill_quantity;
    msg.leaves_quantity = order.remaining_quantity;
    msg.timestamp = get_timestamp();
    
    send_message(&msg, sizeof(msg));
}

void OrderManager::send_cancel_ack(const OrderState& order) {
    protocol::OrderRejectMessage msg;
    msg.header.set_type(protocol::MessageType::ORDER_CANCELLED);
    msg.header.sequence = generate_sequence();
    msg.client_order_id = order.client_order_id;
    msg.user_id = order.user_id;
    msg.reason = 0;
    msg.set_text("Order cancelled");
    msg.timestamp = get_timestamp();
    
    send_message(&msg, sizeof(msg));
}

void OrderManager::send_trade(const std::string& symbol, uint64_t trade_id,
                              uint64_t price, uint64_t quantity) {
    protocol::TradeMessage msg;
    msg.header.sequence = generate_sequence();
    msg.set_symbol(symbol);
    msg.trade_id = trade_id;
    msg.price = price;
    msg.quantity = quantity;
    msg.timestamp = get_timestamp();
    
    send_message(&msg, sizeof(msg));
}

void OrderManager::send_quote(const std::string& symbol, uint32_t bid_price,
                              uint32_t bid_quantity, uint32_t ask_price, uint32_t ask_quantity) {
    protocol::QuoteMessage msg;
    msg.header.sequence = generate_sequence();
    msg.set_symbol(symbol);
    msg.bid_price = bid_price;
    msg.bid_quantity = bid_quantity;
    msg.ask_price = ask_price;
    msg.ask_quantity = ask_quantity;
    msg.timestamp = get_timestamp();
    
    send_message(&msg, sizeof(msg));
}

// =============================================================================
// ORDER VALIDATION
// =============================================================================

protocol::RejectReason OrderManager::validate_new_order(const protocol::NewOrderMessage& msg) {
    std::string symbol = msg.get_symbol();
    if (symbol.empty() || symbol.length() > 15) {
        return protocol::RejectReason::INVALID_SYMBOL;
    }
    
    if (msg.get_order_type() == protocol::OrderType::LIMIT) {
        if (msg.price == 0) {
            return protocol::RejectReason::INVALID_PRICE;
        }
    }
    
    if (msg.quantity == 0) {
        return protocol::RejectReason::INVALID_QUANTITY;
    }
    
    if (msg.user_id == 0) {
        return protocol::RejectReason::SYSTEM_ERROR;
    }
    
    return protocol::RejectReason::NONE;
}

// =============================================================================
// MATCHING ENGINE CALLBACKS
// =============================================================================

void OrderManager::trade_callback(void* user_data,
                                  uint64_t aggressive_order_id,
                                  uint64_t passive_order_id,
                                  uint32_t price,
                                  uint32_t quantity,
                                  uint64_t timestamp) {
    OrderManager* manager = static_cast<OrderManager*>(user_data);
    manager->on_trade(aggressive_order_id, passive_order_id, price, quantity, timestamp);
}

void OrderManager::order_callback(void* user_data,
                                  uint64_t order_id,
                                  mx_order_event_t event,
                                  uint32_t filled_quantity,
                                  uint32_t remaining_quantity) {
    OrderManager* manager = static_cast<OrderManager*>(user_data);
    manager->on_order_event(order_id, event, filled_quantity, remaining_quantity);
}

void OrderManager::on_trade(uint64_t aggressive_order_id,
                           uint64_t passive_order_id,
                           uint32_t price,
                           uint32_t quantity,
                           uint64_t timestamp) {
    (void)timestamp; // Unused for now
    
    stats_.total_executions++;
    stats_.total_volume += quantity;
    
    // Find orders
    auto agg_it = exchange_to_client_.find(aggressive_order_id);
    auto pass_it = exchange_to_client_.find(passive_order_id);
    
    if (agg_it == exchange_to_client_.end() || pass_it == exchange_to_client_.end()) {
        std::cerr << "[OrderManager] Trade for unknown order IDs" << std::endl;
        return;
    }
    
    auto agg_order_it = orders_.find(agg_it->second);
    auto pass_order_it = orders_.find(pass_it->second);
    
    if (agg_order_it == orders_.end() || pass_order_it == orders_.end()) {
        return;
    }
    
    OrderState& agg_order = agg_order_it->second;
    OrderState& pass_order = pass_order_it->second;
    
    // Find symbol for this trade
    std::string symbol = agg_order.symbol;
    auto book_it = books_.find(symbol);
    if (book_it != books_.end()) {
        uint64_t trade_id = ++book_it->second->last_trade_id;
        send_trade(symbol, trade_id, price, quantity);
    }
    
    // Send executions to both sides
    send_execution(agg_order, price, quantity, generate_execution_id());
    send_execution(pass_order, price, quantity, generate_execution_id());
}

void OrderManager::on_order_event(uint64_t order_id,
                                  mx_order_event_t event,
                                  uint32_t filled_quantity,
                                  uint32_t remaining_quantity) {
    auto it = exchange_to_client_.find(order_id);
    if (it == exchange_to_client_.end()) {
        return;
    }
    
    auto order_it = orders_.find(it->second);
    if (order_it == orders_.end()) {
        return;
    }
    
    OrderState& order = order_it->second;
    
    switch (event) {
        case MX_EVENT_ORDER_PARTIAL:
            order.filled_quantity = filled_quantity;
            order.remaining_quantity = remaining_quantity;
            order.status = OrderState::Status::PARTIALLY_FILLED;
            break;
            
        case MX_EVENT_ORDER_FILLED:
            order.filled_quantity = filled_quantity;
            order.remaining_quantity = 0;
            order.status = OrderState::Status::FILLED;
            break;
            
        case MX_EVENT_ORDER_CANCELLED:
            order.status = OrderState::Status::CANCELLED;
            break;
            
        default:
            break;
    }
}

// =============================================================================
// INTERNAL STATE MANAGEMENT
// =============================================================================

void OrderManager::update_order_filled(OrderState& order, uint64_t filled_qty) {
    order.filled_quantity += filled_qty;
    order.remaining_quantity -= filled_qty;
    
    if (order.remaining_quantity == 0) {
        order.status = OrderState::Status::FILLED;
    } else {
        order.status = OrderState::Status::PARTIALLY_FILLED;
    }
}

void OrderManager::update_order_cancelled(OrderState& order) {
    order.status = OrderState::Status::CANCELLED;
}

} // namespace engine
} // namespace matching
