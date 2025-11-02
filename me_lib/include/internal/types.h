/**
 * Internal type definitions
 * Extended structures not exposed in public API
 */

#ifndef MX_INTERNAL_TYPES_H
#define MX_INTERNAL_TYPES_H

#include "common.h"

namespace matchx {

/* Forward declarations */
class Order;
class PriceLevel;
class OrderBook;
class Context;

/* ============================================================================
 * Order State
 * ========================================================================= */

enum class OrderState : uint8_t {
    PENDING_NEW = 0,      // Order created but not yet in book
    ACTIVE = 1,           // Order is active in the book
    PARTIALLY_FILLED = 2, // Order has some fills
    FILLED = 3,           // Order completely filled
    CANCELLED = 4,        // Order cancelled
    REJECTED = 5,         // Order rejected
    EXPIRED = 6,          // Order expired (DAY/GTD)
    TRIGGERED = 7         // Stop order triggered and converted
};

/* ============================================================================
 * Match Result
 * Returned from matching functions
 * ========================================================================= */

struct MatchResult {
    uint32_t matched_quantity;    // How much was matched
    uint32_t remaining_quantity;  // How much remains
    bool fully_matched;           // True if order fully executed
    mx_status_t status;           // Status code
    
    MatchResult() 
        : matched_quantity(0)
        , remaining_quantity(0)
        , fully_matched(false)
        , status(MX_STATUS_OK) {}
};

/* ============================================================================
 * Trade Execution Record
 * Internal record of a trade execution
 * ========================================================================= */

struct Trade {
    OrderId buy_order_id;
    OrderId sell_order_id;
    Price price;
    Quantity quantity;
    Timestamp timestamp;
    
    Trade(OrderId buy_id, OrderId sell_id, Price p, Quantity q, Timestamp ts)
        : buy_order_id(buy_id)
        , sell_order_id(sell_id)
        , price(p)
        , quantity(q)
        , timestamp(ts) {}
};

/* ============================================================================
 * Order Snapshot
 * Lightweight structure for order queries (avoids exposing full Order class)
 * ========================================================================= */

struct OrderSnapshot {
    OrderId order_id;
    Side side;
    OrderType type;
    Price price;
    Price stop_price;
    Quantity total_quantity;
    Quantity filled_quantity;
    Quantity remaining_quantity;
    Quantity display_quantity;
    TimeInForce tif;
    uint32_t flags;
    OrderState state;
    Timestamp created_time;
    Timestamp expire_time;
    
    OrderSnapshot()
        : order_id(INVALID_ORDER_ID)
        , side(MX_SIDE_BUY)
        , type(MX_ORDER_TYPE_LIMIT)
        , price(0)
        , stop_price(0)
        , total_quantity(0)
        , filled_quantity(0)
        , remaining_quantity(0)
        , display_quantity(0)
        , tif(MX_TIF_GTC)
        , flags(0)
        , state(OrderState::PENDING_NEW)
        , created_time(0)
        , expire_time(0) {}
};

/* ============================================================================
 * Price Level Statistics
 * ========================================================================= */

struct PriceLevelStats {
    Price price;
    Quantity total_volume;
    uint32_t order_count;
    
    PriceLevelStats()
        : price(0)
        , total_volume(0)
        , order_count(0) {}
};

/* ============================================================================
 * Order Book Statistics
 * ========================================================================= */

struct OrderBookStats {
    uint32_t total_orders;
    uint32_t bid_levels;
    uint32_t ask_levels;
    uint64_t total_bid_volume;
    uint64_t total_ask_volume;
    Price best_bid;
    Price best_ask;
    
    OrderBookStats()
        : total_orders(0)
        , bid_levels(0)
        , ask_levels(0)
        , total_bid_volume(0)
        , total_ask_volume(0)
        , best_bid(0)
        , best_ask(0) {}
};

/* ============================================================================
 * Callback Wrappers
 * Internal representation of callbacks with user data
 * ========================================================================= */

struct CallbackContext {
    mx_trade_callback_t trade_callback;
    mx_order_callback_t order_callback;
    void* user_data;
    
    CallbackContext()
        : trade_callback(nullptr)
        , order_callback(nullptr)
        , user_data(nullptr) {}
    
    void on_trade(OrderId aggressive_id, OrderId passive_id, 
                  Price price, Quantity qty, Timestamp timestamp) const {
        if (trade_callback) {
            trade_callback(user_data, aggressive_id, passive_id, price, qty, timestamp);
        }
    }
    
    void on_order_event(OrderId order_id, mx_order_event_t event,
                        Quantity filled, Quantity remaining) const {
        if (order_callback) {
            order_callback(user_data, order_id, event, filled, remaining);
        }
    }
};

/* ============================================================================
 * Configuration
 * ========================================================================= */

struct OrderBookConfig {
    // Price bounds (for array-based price levels)
    Price min_price;
    Price max_price;
    Price tick_size;
    
    // Capacity hints
    uint32_t expected_max_orders;
    uint32_t expected_price_levels;
    
    // Features
    bool enable_stop_orders;
    bool enable_iceberg_orders;
    bool enable_time_expiry;
    
    OrderBookConfig()
        : min_price(0)
        , max_price(UINT32_MAX)
        , tick_size(1)
        , expected_max_orders(10000)
        , expected_price_levels(1000)
        , enable_stop_orders(true)
        , enable_iceberg_orders(true)
        , enable_time_expiry(true) {}
};

} // namespace matchx

#endif // MX_INTERNAL_TYPES_H
