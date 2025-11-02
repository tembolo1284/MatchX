/**
 * PriceLevel - manages all orders at a specific price
 * Uses intrusive linked list for strict time priority (FIFO)
 */

#ifndef MX_INTERNAL_CORE_PRICE_LEVEL_H
#define MX_INTERNAL_CORE_PRICE_LEVEL_H

#include "../common.h"
#include "../types.h"
#include "../utils/intrusive_list.h"
#include "order.h"

namespace matchx {

/* ============================================================================
 * PriceLevel Class
 * Represents all orders at a specific price level
 * ========================================================================= */

class PriceLevel {
private:
    Price price_;                           // The price for this level
    IntrusiveList<Order> orders_;          // Orders in time priority (FIFO)
    Quantity total_volume_;                // Total quantity at this level
    Quantity visible_volume_;              // Visible quantity (for depth display)
    
    MX_IMPLEMENTS_ALLOCATORS

public:
    /* ========================================================================
     * Constructors
     * ===================================================================== */
    
    explicit PriceLevel(Price price = 0)
        : price_(price)
        , orders_()
        , total_volume_(0)
        , visible_volume_(0) {}
    
    ~PriceLevel() {
        // Note: Orders are owned by OrderPool, not by PriceLevel
        orders_.clear();
    }
    
    // Non-copyable
    PriceLevel(const PriceLevel&) = delete;
    PriceLevel& operator=(const PriceLevel&) = delete;
    
    // Moveable
    PriceLevel(PriceLevel&& other) noexcept
        : price_(other.price_)
        , orders_(std::move(other.orders_))
        , total_volume_(other.total_volume_)
        , visible_volume_(other.visible_volume_) {
        
        other.total_volume_ = 0;
        other.visible_volume_ = 0;
    }
    
    PriceLevel& operator=(PriceLevel&& other) noexcept {
        if (this != &other) {
            price_ = other.price_;
            orders_ = std::move(other.orders_);
            total_volume_ = other.total_volume_;
            visible_volume_ = other.visible_volume_;
            
            other.total_volume_ = 0;
            other.visible_volume_ = 0;
        }
        return *this;
    }
    
    /* ========================================================================
     * Getters
     * ===================================================================== */
    
    Price price() const { return price_; }
    Quantity total_volume() const { return total_volume_; }
    Quantity visible_volume() const { return visible_volume_; }
    uint32_t order_count() const { return orders_.size(); }
    bool empty() const { return orders_.empty(); }
    
    Order* front() const { return orders_.head(); }
    Order* back() const { return orders_.tail(); }
    
    /* ========================================================================
     * Order Management
     * ===================================================================== */
    
    /**
     * Add an order to the end of the queue (time priority)
     */
    void add_order(Order* order) {
        MX_ASSERT(order != nullptr);
        MX_ASSERT(order->price() == price_);
        MX_ASSERT(!order->is_linked()); // Order must not be in another list
        
        orders_.push_back(order);
        total_volume_ += order->remaining_quantity();
        visible_volume_ += order->visible_quantity();
    }
    
    /**
     * Remove an order from this level - O(1) thanks to intrusive list!
     */
    void remove_order(Order* order) {
        MX_ASSERT(order != nullptr);
        MX_ASSERT(order->is_linked());
        
        total_volume_ -= order->remaining_quantity();
        visible_volume_ -= order->visible_quantity();
        orders_.remove(order);
    }
    
    /**
     * Update volume after an order is partially filled
     * Call this after modifying order quantity
     */
    void update_order_volume(Order* order, Quantity old_remaining, Quantity old_visible) {
        MX_ASSERT(order != nullptr);
        
        // Adjust volumes
        total_volume_ = total_volume_ - old_remaining + order->remaining_quantity();
        visible_volume_ = visible_volume_ - old_visible + order->visible_quantity();
        
        // For iceberg orders, if visible portion refilled, move to back of queue
        if (order->is_iceberg() && order->visible_quantity() > old_visible) {
            // Iceberg refresh - move to back (loses time priority for new visible portion)
            orders_.remove(order);
            orders_.push_back(order);
        }
    }
    
    /**
     * Get next order for matching (peek without removing)
     */
    Order* peek_next_order() const {
        return orders_.head();
    }
    
    /**
     * Check if this level has enough visible volume for a quantity
     */
    bool has_visible_volume(Quantity quantity) const {
        return visible_volume_ >= quantity;
    }
    
    /**
     * Check if this level has enough total volume for a quantity
     */
    bool has_total_volume(Quantity quantity) const {
        return total_volume_ >= quantity;
    }
    
    /* ========================================================================
     * Matching Operations
     * ===================================================================== */
    
    /**
     * Match against orders at this level
     * Returns total quantity matched
     * 
     * @param aggressive_order  The incoming order
     * @param max_quantity      Maximum quantity to match
     * @param on_trade          Callback for each trade
     * @param timestamp         Current timestamp
     */
    template<typename TradeCallback>
    Quantity match_orders(Order* aggressive_order, Quantity max_quantity,
                         TradeCallback on_trade, Timestamp timestamp) {
        Quantity total_matched = 0;
        
        while (total_matched < max_quantity && !orders_.empty()) {
            Order* passive_order = orders_.head();
            
            // Calculate match quantity
            Quantity aggressive_remaining = max_quantity - total_matched;
            Quantity passive_remaining = passive_order->remaining_quantity();
            Quantity match_qty = MX_MIN(aggressive_remaining, passive_remaining);
            
            // Execute the trade
            Price execution_price = passive_order->price();
            
            // Fill both orders
            aggressive_order->fill(match_qty);
            passive_order->fill(match_qty);
            
            // Update volumes
            total_volume_ -= match_qty;
            visible_volume_ -= MX_MIN(match_qty, passive_order->visible_quantity());
            
            // Notify callback
            if (aggressive_order->is_buy()) {
                on_trade(aggressive_order->order_id(), passive_order->order_id(),
                        execution_price, match_qty, timestamp);
            } else {
                on_trade(passive_order->order_id(), aggressive_order->order_id(),
                        execution_price, match_qty, timestamp);
            }
            
            // Remove passive order if fully filled
            if (passive_order->is_filled()) {
                orders_.pop_front();
            } else {
                // Partially filled - check if iceberg needs refresh
                if (passive_order->is_iceberg() && passive_order->visible_quantity() == 0) {
                    // Move to back to replenish visible portion
                    orders_.remove(passive_order);
                    orders_.push_back(passive_order);
                    visible_volume_ += passive_order->visible_quantity();
                }
            }
            
            total_matched += match_qty;
            
            // Stop if aggressive order is filled
            if (aggressive_order->is_filled()) {
                break;
            }
        }
        
        return total_matched;
    }
    
    /**
     * Check if an AON (All-or-None) order can be filled at this level
     */
    bool can_fill_aon(Quantity quantity) const {
        // For AON, we need to check if there's enough total volume
        // We can't just check visible_volume because hidden orders count too
        return total_volume_ >= quantity;
    }
    
    /**
     * Calculate how much of FOK order can be filled
     * Returns quantity that can be filled immediately
     */
    Quantity calculate_fok_fill(Quantity quantity) const {
        Quantity available = 0;
        
        for (Order* order : orders_) {
            available += order->remaining_quantity();
            if (available >= quantity) {
                return quantity; // Can fill completely
            }
        }
        
        return 0; // Cannot fill completely, so FOK would be rejected
    }
    
    /* ========================================================================
     * Statistics
     * ===================================================================== */
    
    /**
     * Get statistics about this level
     */
    PriceLevelStats get_stats() const {
        PriceLevelStats stats;
        stats.price = price_;
        stats.total_volume = total_volume_;
        stats.order_count = orders_.size();
        return stats;
    }
    
    /* ========================================================================
     * Iteration
     * ===================================================================== */
    
    /**
     * Iterate over all orders at this level
     */
    template<typename Func>
    void for_each_order(Func func) const {
        for (Order* order : orders_) {
            func(order);
        }
    }
    
    /**
     * Find an order by ID (linear search, but lists are typically short)
     */
    Order* find_order(OrderId order_id) const {
        for (Order* order : orders_) {
            if (order->order_id() == order_id) {
                return order;
            }
        }
        return nullptr;
    }
    
    /* ========================================================================
     * Debug
     * ===================================================================== */
    
#ifdef MX_DEBUG
    void validate() const {
        Quantity calculated_volume = 0;
        Quantity calculated_visible = 0;
        
        for (const Order* order : orders_) {
            MX_ASSERT(order->price() == price_);
            MX_ASSERT(order->is_active() || order->is_partially_filled());
            calculated_volume += order->remaining_quantity();
            calculated_visible += order->visible_quantity();
        }
        
        MX_ASSERT(calculated_volume == total_volume_);
        MX_ASSERT(calculated_visible == visible_volume_);
    }
#else
    void validate() const {}
#endif
};

} // namespace matchx

#endif // MX_INTERNAL_CORE_PRICE_LEVEL_H
