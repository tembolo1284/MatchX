/**
 * Order class - represents a single order in the book
 * Uses intrusive linked list for zero-allocation queue management
 */

#ifndef MX_INTERNAL_CORE_ORDER_H
#define MX_INTERNAL_CORE_ORDER_H

#include "../common.h"
#include "../types.h"
#include "../utils/intrusive_list.h"

namespace matchx {

/* ============================================================================
 * Order Class
 * Represents a single order with all its attributes
 * ========================================================================= */

class Order : public IntrusiveListNode<Order> {
private:
    // Order identification
    OrderId order_id_;
    
    // Order attributes
    Side side_;
    OrderType order_type_;
    OrderState state_;
    TimeInForce time_in_force_;
    uint32_t flags_;
    
    // Pricing
    Price price_;           // Limit price (0 for market orders)
    Price stop_price_;      // Stop trigger price (0 for non-stop orders)
    
    // Quantities
    Quantity total_quantity_;      // Original order quantity
    Quantity filled_quantity_;     // Amount already filled
    Quantity display_quantity_;    // Visible quantity (for iceberg)
    Quantity visible_filled_;      // How much of visible portion is filled
    
    // Timing
    Timestamp created_time_;       // When order was created
    Timestamp expire_time_;        // Expiration time (0 = no expiry)
    
    MX_IMPLEMENTS_ALLOCATORS

public:
    /* ========================================================================
     * Constructors
     * ===================================================================== */
    
    Order(OrderId id, Side side, OrderType type, Price price, 
          Quantity quantity, Timestamp created)
        : order_id_(id)
        , side_(side)
        , order_type_(type)
        , state_(OrderState::PENDING_NEW)
        , time_in_force_(MX_TIF_GTC)
        , flags_(MX_ORDER_FLAG_NONE)
        , price_(price)
        , stop_price_(0)
        , total_quantity_(quantity)
        , filled_quantity_(0)
        , display_quantity_(0) // 0 means show all
        , visible_filled_(0)
        , created_time_(created)
        , expire_time_(0) {}
    
    // Full constructor with all parameters
    Order(OrderId id, Side side, OrderType type, Price price, Price stop_price,
          Quantity quantity, Quantity display_qty, TimeInForce tif, 
          uint32_t flags, Timestamp created, Timestamp expire)
        : order_id_(id)
        , side_(side)
        , order_type_(type)
        , state_(OrderState::PENDING_NEW)
        , time_in_force_(tif)
        , flags_(flags)
        , price_(price)
        , stop_price_(stop_price)
        , total_quantity_(quantity)
        , filled_quantity_(0)
        , display_quantity_(display_qty)
        , visible_filled_(0)
        , created_time_(created)
        , expire_time_(expire) {}
    
    ~Order() = default;
    
    // Non-copyable (orders have unique identity)
    Order(const Order&) = delete;
    Order& operator=(const Order&) = delete;
    
    /* ========================================================================
     * Getters
     * ===================================================================== */
    
    OrderId order_id() const { return order_id_; }
    Side side() const { return side_; }
    OrderType order_type() const { return order_type_; }
    OrderState state() const { return state_; }
    TimeInForce time_in_force() const { return time_in_force_; }
    uint32_t flags() const { return flags_; }
    
    Price price() const { return price_; }
    Price stop_price() const { return stop_price_; }
    
    Quantity total_quantity() const { return total_quantity_; }
    Quantity filled_quantity() const { return filled_quantity_; }
    Quantity remaining_quantity() const { return total_quantity_ - filled_quantity_; }
    
    Quantity display_quantity() const { return display_quantity_; }
    Quantity visible_quantity() const {
        // If display_quantity is 0, show all remaining
        if (display_quantity_ == 0) {
            return remaining_quantity();
        }
        // Otherwise show the display amount minus what's been filled of it
        return (display_quantity_ > visible_filled_) ? 
               (display_quantity_ - visible_filled_) : 0;
    }
    
    Timestamp created_time() const { return created_time_; }
    Timestamp expire_time() const { return expire_time_; }
    
    /* ========================================================================
     * State Queries
     * ===================================================================== */
    
    bool is_buy() const { return side_ == MX_SIDE_BUY; }
    bool is_sell() const { return side_ == MX_SIDE_SELL; }
    
    bool is_limit() const { return order_type_ == MX_ORDER_TYPE_LIMIT; }
    bool is_market() const { return order_type_ == MX_ORDER_TYPE_MARKET; }
    bool is_stop() const { 
        return order_type_ == MX_ORDER_TYPE_STOP || 
               order_type_ == MX_ORDER_TYPE_STOP_LIMIT; 
    }
    
    bool is_active() const { return state_ == OrderState::ACTIVE; }
    bool is_filled() const { return state_ == OrderState::FILLED; }
    bool is_cancelled() const { return state_ == OrderState::CANCELLED; }
    bool is_partially_filled() const { return state_ == OrderState::PARTIALLY_FILLED; }
    
    bool is_gtc() const { return time_in_force_ == MX_TIF_GTC; }
    bool is_ioc() const { return time_in_force_ == MX_TIF_IOC; }
    bool is_fok() const { return time_in_force_ == MX_TIF_FOK; }
    bool is_day() const { return time_in_force_ == MX_TIF_DAY; }
    bool is_gtd() const { return time_in_force_ == MX_TIF_GTD; }
    
    bool is_post_only() const { return MX_HAS_BIT(flags_, MX_ORDER_FLAG_POST_ONLY); }
    bool is_hidden() const { return MX_HAS_BIT(flags_, MX_ORDER_FLAG_HIDDEN); }
    bool is_iceberg() const { return display_quantity_ > 0; }
    bool is_aon() const { return MX_HAS_BIT(flags_, MX_ORDER_FLAG_AON); }
    bool is_reduce_only() const { return MX_HAS_BIT(flags_, MX_ORDER_FLAG_REDUCE_ONLY); }
    
    bool has_expiry() const { return expire_time_ > 0; }
    bool is_expired(Timestamp current_time) const {
        return has_expiry() && current_time >= expire_time_;
    }
    
    /* ========================================================================
     * Setters (internal state changes)
     * ===================================================================== */
    
    void set_state(OrderState state) { state_ = state; }
    void set_price(Price price) { price_ = price; }
    
    /* ========================================================================
     * Order Operations
     * ===================================================================== */
    
    /**
     * Fill the order (or partially fill it)
     * Returns the actual quantity filled
     */
    Quantity fill(Quantity quantity) {
        Quantity can_fill = MX_MIN(quantity, remaining_quantity());
        
        if (can_fill == 0) {
            return 0;
        }
        
        filled_quantity_ += can_fill;
        
        // Update visible portion for iceberg orders
        if (is_iceberg()) {
            visible_filled_ += can_fill;
            
            // If visible portion is exhausted, replenish it
            if (visible_filled_ >= display_quantity_ && remaining_quantity() > 0) {
                visible_filled_ = 0; // Reset visible counter
            }
        }
        
        // Update state
        if (filled_quantity_ >= total_quantity_) {
            state_ = OrderState::FILLED;
        } else {
            state_ = OrderState::PARTIALLY_FILLED;
        }
        
        return can_fill;
    }
    
    /**
     * Reduce order quantity (for modify operations)
     * Only allows reduction, maintains time priority
     */
    bool reduce_quantity(Quantity new_quantity) {
        if (new_quantity >= total_quantity_) {
            return false; // Can only reduce
        }
        
        if (new_quantity <= filled_quantity_) {
            return false; // Can't reduce below filled amount
        }
        
        total_quantity_ = new_quantity;
        return true;
    }
    
    /**
     * Cancel the order
     */
    void cancel() {
        state_ = OrderState::CANCELLED;
    }
    
    /**
     * Reject the order
     */
    void reject() {
        state_ = OrderState::REJECTED;
    }
    
    /**
     * Trigger a stop order (convert to regular limit/market)
     */
    void trigger_stop() {
        MX_ASSERT(is_stop());
        
        // Convert stop order to regular order
        if (order_type_ == MX_ORDER_TYPE_STOP) {
            order_type_ = MX_ORDER_TYPE_MARKET;
        } else if (order_type_ == MX_ORDER_TYPE_STOP_LIMIT) {
            order_type_ = MX_ORDER_TYPE_LIMIT;
        }
        
        state_ = OrderState::TRIGGERED;
        stop_price_ = 0; // No longer a stop order
    }
    
    /* ========================================================================
     * Utility
     * ===================================================================== */
    
    /**
     * Get a snapshot of this order
     */
    OrderSnapshot snapshot() const {
        OrderSnapshot snap;
        snap.order_id = order_id_;
        snap.side = side_;
        snap.type = order_type_;
        snap.price = price_;
        snap.stop_price = stop_price_;
        snap.total_quantity = total_quantity_;
        snap.filled_quantity = filled_quantity_;
        snap.remaining_quantity = remaining_quantity();
        snap.display_quantity = display_quantity_;
        snap.tif = time_in_force_;
        snap.flags = flags_;
        snap.state = state_;
        snap.created_time = created_time_;
        snap.expire_time = expire_time_;
        return snap;
    }
    
    /**
     * Check if this order can match with another order
     */
    bool can_match_with(const Order* other) const {
        // Must be opposite sides
        if (side_ == other->side_) return false;
        
        // Both must be active
        if (!is_active() || !other->is_active()) return false;
        
        // Price compatibility
        if (is_buy()) {
            // Buy order can match if our price >= sell price
            return price_ >= other->price_;
        } else {
            // Sell order can match if our price <= buy price
            return price_ <= other->price_;
        }
    }
    
    /**
     * Get the execution price when matching with another order
     * Returns the passive order's price (price-time priority)
     */
    Price get_execution_price(const Order* passive_order) const {
        return passive_order->price_;
    }
};

} // namespace matchx

#endif // MX_INTERNAL_CORE_ORDER_H
