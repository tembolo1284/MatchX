/**
 * OrderPool - manages memory allocation for Order objects
 * Uses memory pool for zero-allocation order creation/destruction
 */

#ifndef MX_INTERNAL_CORE_ORDER_POOL_H
#define MX_INTERNAL_CORE_ORDER_POOL_H

#include "../common.h"
#include "../types.h"
#include "../utils/memory_pool.h"
#include "../utils/hash_map.h"
#include "order.h"

namespace matchx {

/* ============================================================================
 * OrderPool Class
 * Manages the lifecycle of Order objects with fast allocation
 * ========================================================================= */

class OrderPool {
private:
    MemoryPool<Order> pool_;                    // Memory pool for orders
    OrderIdMap<Order*> order_lookup_;           // Fast O(1) order lookup by ID
    
    MX_IMPLEMENTS_ALLOCATORS

public:
    /* ========================================================================
     * Constructors
     * ===================================================================== */
    
    explicit OrderPool(size_t initial_capacity = 10000)
        : pool_(initial_capacity)
        , order_lookup_(initial_capacity) {
        
        // Pre-allocate hash map buckets to avoid rehashing
        order_lookup_.reserve(initial_capacity);
    }
    
    ~OrderPool() {
        // Destroy all remaining orders
        clear();
    }
    
    // Non-copyable
    OrderPool(const OrderPool&) = delete;
    OrderPool& operator=(const OrderPool&) = delete;
    
    /* ========================================================================
     * Order Creation
     * ===================================================================== */
    
    /**
     * Create a simple limit order
     */
    Order* create_order(OrderId id, Side side, Price price, 
                       Quantity quantity, Timestamp timestamp) {
        
        // Check for duplicate order ID
        if (MX_UNLIKELY(order_lookup_.contains(id))) {
            return nullptr; // Duplicate order ID
        }
        
        // Allocate and construct order
        Order* order = pool_.construct(id, side, MX_ORDER_TYPE_LIMIT, 
                                      price, quantity, timestamp);
        
        if (MX_LIKELY(order != nullptr)) {
            order->set_state(OrderState::ACTIVE);
            order_lookup_[id] = order;
        }
        
        return order;
    }
    
    /**
     * Create a market order
     */
    Order* create_market_order(OrderId id, Side side, 
                              Quantity quantity, Timestamp timestamp) {
        
        if (MX_UNLIKELY(order_lookup_.contains(id))) {
            return nullptr;
        }
        
        // Market orders have price 0
        Order* order = pool_.construct(id, side, MX_ORDER_TYPE_MARKET,
                                      0, quantity, timestamp);
        
        if (MX_LIKELY(order != nullptr)) {
            order->set_state(OrderState::ACTIVE);
            order_lookup_[id] = order;
        }
        
        return order;
    }
    
    /**
     * Create an order with full parameters
     */
    Order* create_order_full(OrderId id, OrderType type, Side side,
                            Price price, Price stop_price,
                            Quantity quantity, Quantity display_qty,
                            TimeInForce tif, uint32_t flags,
                            Timestamp timestamp, Timestamp expire_time) {
        
        if (MX_UNLIKELY(order_lookup_.contains(id))) {
            return nullptr;
        }
        
        Order* order = pool_.construct(id, side, type, price, stop_price,
                                      quantity, display_qty, tif, flags,
                                      timestamp, expire_time);
        
        if (MX_LIKELY(order != nullptr)) {
            // Stop orders start as pending, others as active
            if (type == MX_ORDER_TYPE_STOP || type == MX_ORDER_TYPE_STOP_LIMIT) {
                order->set_state(OrderState::PENDING_NEW);
            } else {
                order->set_state(OrderState::ACTIVE);
            }
            order_lookup_[id] = order;
        }
        
        return order;
    }
    
    /* ========================================================================
     * Order Destruction
     * ===================================================================== */
    
    /**
     * Destroy an order and return it to the pool
     */
    void destroy_order(Order* order) {
        if (!order) return;
        
        OrderId id = order->order_id();
        
        // Remove from lookup
        order_lookup_.erase(id);
        
        // Return to pool (calls destructor)
        pool_.destroy(order);
    }
    
    /**
     * Destroy an order by ID
     */
    bool destroy_order(OrderId order_id) {
        Order* order = find_order(order_id);
        if (!order) return false;
        
        destroy_order(order);
        return true;
    }
    
    /* ========================================================================
     * Order Lookup
     * ===================================================================== */
    
    /**
     * Find an order by ID - O(1) lookup
     */
    Order* find_order(OrderId order_id) const {
        auto it = order_lookup_.find(order_id);
        return (it != order_lookup_.end()) ? it->second : nullptr;
    }
    
    /**
     * Check if an order exists
     */
    bool has_order(OrderId order_id) const {
        return order_lookup_.contains(order_id);
    }
    
    /**
     * Get order snapshot (safe copy of order data)
     */
    bool get_order_snapshot(OrderId order_id, OrderSnapshot& snapshot) const {
        Order* order = find_order(order_id);
        if (!order) return false;
        
        snapshot = order->snapshot();
        return true;
    }
    
    /* ========================================================================
     * Statistics
     * ===================================================================== */
    
    size_t active_order_count() const { return order_lookup_.size(); }
    size_t pool_capacity() const { return pool_.capacity(); }
    size_t pool_allocated() const { return pool_.allocated(); }
    size_t pool_available() const { return pool_.available(); }
    
    size_t memory_usage() const {
        return pool_.memory_usage();
    }
    
    /* ========================================================================
     * Batch Operations
     * ===================================================================== */
    
    /**
     * Clear all orders (for orderbook reset)
     */
    void clear() {
        // Destroy all orders
        for (auto& pair : order_lookup_) {
            pool_.destroy(pair.second);
        }
        order_lookup_.clear();
    }
    
    /**
     * Pre-allocate capacity for expected number of orders
     */
    void reserve(size_t count) {
        pool_.reserve(count);
        order_lookup_.reserve(count);
    }
    
    /**
     * Find and collect expired orders
     * Returns list of expired order IDs
     */
    template<typename OutputIterator>
    size_t find_expired_orders(Timestamp current_time, OutputIterator out) const {
        size_t count = 0;
        
        for (const auto& pair : order_lookup_) {
            Order* order = pair.second;
            if (order->is_expired(current_time)) {
                *out++ = order->order_id();
                ++count;
            }
        }
        
        return count;
    }
    
    /**
     * Iterate over all orders
     */
    template<typename Func>
    void for_each_order(Func func) const {
        for (const auto& pair : order_lookup_) {
            func(pair.second);
        }
    }
    
    /**
     * Iterate over orders matching a predicate
     */
    template<typename Predicate, typename Func>
    void for_each_order_if(Predicate pred, Func func) const {
        for (const auto& pair : order_lookup_) {
            Order* order = pair.second;
            if (pred(order)) {
                func(order);
            }
        }
    }
    
    /* ========================================================================
     * Debug
     * ===================================================================== */
    
#ifdef MX_DEBUG
    void validate() const {
        // Verify all orders in lookup are valid
        for (const auto& pair : order_lookup_) {
            MX_ASSERT(pair.first == pair.second->order_id());
            MX_ASSERT(pair.second != nullptr);
        }
    }
    
    void print_stats() const {
        MX_DEBUG_PRINT("OrderPool Stats:");
        MX_DEBUG_PRINT("  Active orders: %zu", active_order_count());
        MX_DEBUG_PRINT("  Pool capacity: %zu", pool_capacity());
        MX_DEBUG_PRINT("  Pool allocated: %zu", pool_allocated());
        MX_DEBUG_PRINT("  Memory usage: %zu bytes", memory_usage());
    }
#else
    void validate() const {}
    void print_stats() const {}
#endif
};

} // namespace matchx

#endif // MX_INTERNAL_CORE_ORDER_POOL_H
