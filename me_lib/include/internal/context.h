/**
 * Context - global state container
 * Holds callbacks, configuration, and timestamp
 * No global state - everything goes through context!
 */

#ifndef MX_INTERNAL_CONTEXT_H
#define MX_INTERNAL_CONTEXT_H

#include "common.h"
#include "types.h"
#include <ctime>

namespace matchx {

/* ============================================================================
 * Context Class
 * Following the article's principle: NO GLOBAL STATE!
 * Everything goes through a context object
 * ========================================================================= */

class Context {
private:
    // Callbacks
    CallbackContext callbacks_;
    
    // Configuration
    OrderBookConfig config_;
    
    // Timing
    Timestamp current_timestamp_;
    bool use_system_time_;
    
    MX_IMPLEMENTS_ALLOCATORS

public:
    /* ========================================================================
     * Constructors
     * ===================================================================== */
    
    Context()
        : callbacks_()
        , config_()
        , current_timestamp_(0)
        , use_system_time_(true) {
        
        // Initialize with system time
        update_timestamp();
    }
    
    ~Context() = default;
    
    // Non-copyable
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
    
    /* ========================================================================
     * Callback Management
     * ===================================================================== */
    
    void set_callbacks(mx_trade_callback_t trade_cb, 
                      mx_order_callback_t order_cb,
                      void* user_data) {
        callbacks_.trade_callback = trade_cb;
        callbacks_.order_callback = order_cb;
        callbacks_.user_data = user_data;
    }
    
    void set_trade_callback(mx_trade_callback_t callback, void* user_data) {
        callbacks_.trade_callback = callback;
        callbacks_.user_data = user_data;
    }
    
    void set_order_callback(mx_order_callback_t callback, void* user_data) {
        callbacks_.order_callback = callback;
        callbacks_.user_data = user_data;
    }
    
    const CallbackContext& callbacks() const { return callbacks_; }
    
    /* ========================================================================
     * Configuration
     * ===================================================================== */
    
    OrderBookConfig& config() { return config_; }
    const OrderBookConfig& config() const { return config_; }
    
    void set_price_bounds(Price min_price, Price max_price, Price tick_size) {
        config_.min_price = min_price;
        config_.max_price = max_price;
        config_.tick_size = tick_size;
    }
    
    void set_capacity_hints(uint32_t max_orders, uint32_t price_levels) {
        config_.expected_max_orders = max_orders;
        config_.expected_price_levels = price_levels;
    }
    
    void enable_stop_orders(bool enable) {
        config_.enable_stop_orders = enable;
    }
    
    void enable_iceberg_orders(bool enable) {
        config_.enable_iceberg_orders = enable;
    }
    
    void enable_time_expiry(bool enable) {
        config_.enable_time_expiry = enable;
    }
    
    /* ========================================================================
     * Timestamp Management
     * ===================================================================== */
    
    /**
     * Get current timestamp (nanoseconds)
     */
    Timestamp get_timestamp() const {
        return current_timestamp_;
    }
    
    /**
     * Set timestamp manually (for backtesting/simulation)
     */
    void set_timestamp(Timestamp timestamp) {
        current_timestamp_ = timestamp;
        use_system_time_ = false;
    }
    
    /**
     * Enable automatic system time updates
     */
    void use_system_time(bool enable) {
        use_system_time_ = enable;
        if (enable) {
            update_timestamp();
        }
    }
    
    /**
     * Update timestamp to current system time
     */
    void update_timestamp() {
        if (use_system_time_) {
            current_timestamp_ = get_system_timestamp();
        }
    }
    
    /* ========================================================================
     * Statistics
     * ===================================================================== */
    
    /**
     * Get memory usage across all contexts (would need global tracking)
     */
    size_t get_memory_usage() const {
        // This is a placeholder - actual implementation would track allocations
        return 0;
    }
    
private:
    /* ========================================================================
     * Internal Helpers
     * ===================================================================== */
    
    /**
     * Get high-resolution system timestamp in nanoseconds
     */
    static Timestamp get_system_timestamp() {
#if defined(_WIN32)
        // Windows: Use QueryPerformanceCounter
        LARGE_INTEGER frequency;
        LARGE_INTEGER counter;
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&counter);
        return static_cast<Timestamp>((counter.QuadPart * 1000000000ULL) / frequency.QuadPart);
#elif defined(__linux__) || defined(__APPLE__)
        // Linux/Mac: Use clock_gettime with CLOCK_MONOTONIC
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<Timestamp>(ts.tv_sec) * 1000000000ULL + static_cast<Timestamp>(ts.tv_nsec);
#else
        // Fallback: Use standard library (microsecond resolution)
        auto now = std::chrono::high_resolution_clock::now();
        auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch());
        return static_cast<Timestamp>(nanos.count());
#endif
    }
};

} // namespace matchx

#endif // MX_INTERNAL_CONTEXT_H
