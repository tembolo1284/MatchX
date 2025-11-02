/**
 * OrderBook - the main matching engine
 * Manages price levels, executes trades, handles all order types
 * This is where the magic happens!
 */

#ifndef MX_INTERNAL_CORE_ORDER_BOOK_H
#define MX_INTERNAL_CORE_ORDER_BOOK_H

#include "../common.h"
#include "../types.h"
#include "../utils/hash_map.h"
#include "order.h"
#include "price_level.h"
#include "order_pool.h"
#include <map>
#include <string>
#include <vector>

namespace matchx {

// Forward declaration
class Context;

/* ============================================================================
 * OrderBook Class
 * The core matching engine that maintains price-time priority
 * ========================================================================= */

class OrderBook {
private:
    // Symbol identification
    char* symbol_;
    
    // Context (callbacks, config)
    Context* context_;
    
    // Order management
    OrderPool order_pool_;
    
    // Price levels - using std::map for now (could optimize to array for bounded prices)
    // Key insight: std::map keeps prices sorted automatically!
    std::map<Price, PriceLevel, std::greater<Price>, ProxyAllocator<std::pair<const Price, PriceLevel>>> bid_levels_;  // Descending (highest first)
    std::map<Price, PriceLevel, std::less<Price>, ProxyAllocator<std::pair<const Price, PriceLevel>>> ask_levels_;     // Ascending (lowest first)
    
    // Stop orders (not in main book yet)
    OrderIdMap<Order*> stop_orders_;
    
    // Best prices (cached for O(1) access)
    Price best_bid_;
    Price best_ask_;
    
    // Statistics
    uint64_t total_trades_;
    uint64_t total_volume_;
    
    MX_IMPLEMENTS_ALLOCATORS

public:
    /* ========================================================================
     * Constructors
     * ===================================================================== */
    
    OrderBook(Context* ctx, const char* symbol);
    ~OrderBook();
    
    // Non-copyable
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    
    /* ========================================================================
     * Basic Information
     * ===================================================================== */
    
    const char* symbol() const { return symbol_; }
    Context* context() const { return context_; }
    
    /* ========================================================================
     * Order Operations - Simple API
     * ===================================================================== */
    
    /**
     * Add a simple limit order (GTC)
     */
    mx_status_t add_limit_order(OrderId order_id, Side side, 
                                Price price, Quantity quantity);
    
    /**
     * Add a market order
     */
    mx_status_t add_market_order(OrderId order_id, Side side, Quantity quantity);
    
    /**
     * Cancel an order
     */
    mx_status_t cancel_order(OrderId order_id);
    
    /**
     * Modify an order (reduce quantity only, maintains time priority)
     */
    mx_status_t modify_order(OrderId order_id, Quantity new_quantity);
    
    /**
     * Replace an order (cancel + add, loses time priority)
     */
    mx_status_t replace_order(OrderId old_order_id, OrderId new_order_id,
                             Price new_price, Quantity new_quantity);
    
    /* ========================================================================
     * Order Operations - Advanced API
     * ===================================================================== */
    
    /**
     * Add an order with full control over all parameters
     */
    mx_status_t add_order(OrderId order_id, OrderType order_type, Side side,
                         Price price, Price stop_price, Quantity quantity,
                         Quantity display_qty, TimeInForce tif, uint32_t flags,
                         uint64_t expire_time);
    
    /* ========================================================================
     * Market Data Queries
     * ===================================================================== */
    
    Price get_best_bid() const { return best_bid_; }
    Price get_best_ask() const { return best_ask_; }
    
    Price get_spread() const {
        if (best_bid_ == 0 || best_ask_ == 0) return 0;
        return best_ask_ - best_bid_;
    }
    
    Price get_mid_price() const {
        if (best_bid_ == 0 || best_ask_ == 0) return 0;
        return (best_bid_ + best_ask_) / 2;
    }
    
    Quantity get_volume_at_price(Side side, Price price) const;
    uint64_t get_depth(Side side, uint32_t num_levels) const;
    
    /* ========================================================================
     * Order Queries
     * ===================================================================== */
    
    bool has_order(OrderId order_id) const {
        return order_pool_.has_order(order_id);
    }
    
    bool get_order_info(OrderId order_id, OrderSnapshot& snapshot) const {
        return order_pool_.get_order_snapshot(order_id, snapshot);
    }
    
    /* ========================================================================
     * Statistics
     * ===================================================================== */
    
    OrderBookStats get_stats() const;
    
    uint32_t get_bid_level_count() const { return static_cast<uint32_t>(bid_levels_.size()); }
    uint32_t get_ask_level_count() const { return static_cast<uint32_t>(ask_levels_.size()); }
    uint32_t get_total_order_count() const { return static_cast<uint32_t>(order_pool_.active_order_count()); }
    
    uint64_t get_total_trades() const { return total_trades_; }
    uint64_t get_total_volume() const { return total_volume_; }
    
    /* ========================================================================
     * Administrative
     * ===================================================================== */
    
    /**
     * Clear all orders from the book
     */
    void clear();
    
    /**
     * Process expired orders (DAY/GTD)
     */
    uint32_t process_expirations(Timestamp current_time);
    
    /**
     * Process stop orders that may have been triggered
     */
    uint32_t process_stops();
    
private:
    /* ========================================================================
     * Internal Order Processing
     * ===================================================================== */
    
    /**
     * Process a new order (main entry point)
     */
    mx_status_t process_new_order(Order* order);
    
    /**
     * Match an order against the book
     */
    MatchResult match_order(Order* order);
    
    /**
     * Match a limit order
     */
    MatchResult match_limit_order(Order* order);
    
    /**
     * Match a market order
     */
    MatchResult match_market_order(Order* order);
    
    /**
     * Add order to book (after matching or if no match)
     */
    void add_to_book(Order* order);
    
    /**
     * Remove order from book
     */
    void remove_from_book(Order* order);
    
    /* ========================================================================
     * Price Level Management
     * ===================================================================== */
    
    /**
     * Get or create a price level
     */
    PriceLevel* get_or_create_level(Side side, Price price);
    
    /**
     * Get a price level (returns nullptr if doesn't exist)
     */
    PriceLevel* get_level(Side side, Price price);
    const PriceLevel* get_level(Side side, Price price) const;
    
    /**
     * Remove a price level if empty
     */
    void remove_level_if_empty(Side side, Price price);
    
    /**
     * Update best bid/ask after level changes
     */
    void update_best_bid();
    void update_best_ask();
    
    /* ========================================================================
     * Special Order Type Handling
     * ===================================================================== */
    
    /**
     * Check if POST_ONLY order would match
     */
    bool would_match_immediately(const Order* order) const;
    
    /**
     * Check if FOK order can be filled completely
     */
    bool can_fill_fok(const Order* order, Quantity& available_quantity) const;
    
    /**
     * Check if AON order can be filled
     */
    bool can_fill_aon(const Order* order) const;
    
    /**
     * Handle IOC order (match and cancel remainder)
     */
    MatchResult handle_ioc_order(Order* order);
    
    /**
     * Handle FOK order (all or nothing)
     */
    MatchResult handle_fok_order(Order* order);
    
    /**
     * Handle stop order
     */
    mx_status_t handle_stop_order(Order* order);
    
    /**
     * Check if stop should be triggered
     */
    bool should_trigger_stop(const Order* stop_order) const;
    
    /* ========================================================================
     * Callbacks
     * ===================================================================== */
    
    /**
     * Notify trade callback
     */
    void notify_trade(OrderId aggressive_id, OrderId passive_id,
                     Price price, Quantity quantity, Timestamp timestamp);
    
    /**
     * Notify order event callback
     */
    void notify_order_event(OrderId order_id, mx_order_event_t event,
                           Quantity filled, Quantity remaining);
    
    /**
     * Get current timestamp from context
     */
    Timestamp get_current_timestamp() const;
    
    /* ========================================================================
     * Validation
     * ===================================================================== */
    
    /**
     * Validate order parameters
     */
    mx_status_t validate_order(OrderId order_id, OrderType type, Side side,
                               Price price, Price stop_price, Quantity quantity,
                               TimeInForce tif, uint32_t flags) const;
    
    /* ========================================================================
     * Debug
     * ===================================================================== */
    
#ifdef MX_DEBUG
public:
    void validate() const;
    void print_book(uint32_t levels = 5) const;
    void print_stats() const;
#else
    void validate() const {}
    void print_book(uint32_t levels = 5) const { MX_UNUSED(levels); }
    void print_stats() const {}
#endif
};

/* ============================================================================
 * Inline Helper Functions
 * ========================================================================= */

/**
 * Get the opposite side of an order book
 */
inline Side opposite_side(Side side) {
    return (side == MX_SIDE_BUY) ? MX_SIDE_SELL : MX_SIDE_BUY;
}

/**
 * Check if buy can match with sell at given prices
 */
inline bool can_match(Price buy_price, Price sell_price) {
    return buy_price >= sell_price;
}

/**
 * Determine execution price (passive order's price in price-time priority)
 */
inline Price execution_price(const Order* aggressive, const Order* passive) {
    return passive->price();
}

} // namespace matchx

#endif // MX_INTERNAL_CORE_ORDER_BOOK_H
