/**
 * Advanced Usage Example - C++ example using advanced features
 * Demonstrates IOC, FOK, Iceberg, Stop orders
 */

#include "matchengine.h"
#include <iostream>
#include <iomanip>
#include <vector>

class TradingSimulator {
private:
    mx_context_t* ctx_;
    mx_order_book_t* book_;
    
    struct TradeRecord {
        uint64_t buy_id;
        uint64_t sell_id;
        uint32_t price;
        uint32_t quantity;
    };
    
    std::vector<TradeRecord> trades_;
    
    static void trade_callback(void* user_data, uint64_t buy_id, uint64_t sell_id,
                              uint32_t price, uint32_t quantity, uint64_t timestamp) {
        (void)timestamp;
        auto* sim = static_cast<TradingSimulator*>(user_data);
        sim->trades_.push_back({buy_id, sell_id, price, quantity});
        
        std::cout << "  ✓ TRADE: #" << buy_id << " × #" << sell_id 
                  << " @ $" << std::fixed << std::setprecision(2) << (price / 100.0)
                  << " for " << quantity << " shares\n";
    }
    
    static void order_callback(void* user_data, uint64_t order_id, mx_order_event_t event,
                              uint32_t filled_qty, uint32_t remaining_qty) {
        (void)user_data;
        
        const char* events[] = {"ACCEPTED", "REJECTED", "FILLED", "PARTIAL", 
                               "CANCELLED", "EXPIRED", "TRIGGERED"};
        
        std::cout << "  → Order #" << order_id << ": " << events[event]
                  << " (filled: " << filled_qty << ", remaining: " << remaining_qty << ")\n";
    }

public:
    TradingSimulator() {
        ctx_ = mx_context_new();
        mx_context_set_callbacks(ctx_, trade_callback, order_callback, this);
        book_ = mx_order_book_new(ctx_, "BTCUSD");
    }
    
    ~TradingSimulator() {
        mx_order_book_free(book_);
        mx_context_free(ctx_);
    }
    
    void print_market_data() {
        uint32_t bid = mx_order_book_get_best_bid(book_);
        uint32_t ask = mx_order_book_get_best_ask(book_);
        
        if (bid > 0 && ask > 0) {
            std::cout << "  Market: $" << std::fixed << std::setprecision(2) 
                      << (bid / 100.0) << " × $" << (ask / 100.0) << "\n";
        } else if (bid > 0) {
            std::cout << "  Best Bid: $" << (bid / 100.0) << "\n";
        } else if (ask > 0) {
            std::cout << "  Best Ask: $" << (ask / 100.0) << "\n";
        } else {
            std::cout << "  Market: No quotes\n";
        }
    }
    
    void example_ioc() {
        std::cout << "\n=== IOC (Immediate or Cancel) Example ===\n";
        
        // Setup: passive orders
        mx_order_book_add_limit(book_, 100, MX_SIDE_SELL, 5000000, 50);  // $50k
        mx_order_book_add_limit(book_, 101, MX_SIDE_SELL, 5010000, 50);  // $50.1k
        
        std::cout << "\nIOC Buy 75 @ $50.1k (should match 50, cancel 25):\n";
        mx_order_book_add_order(book_, 200, MX_ORDER_TYPE_LIMIT, MX_SIDE_BUY,
                               5010000, 0, 75, 0, MX_TIF_IOC, MX_ORDER_FLAG_NONE, 0);
        
        trades_.clear();
        mx_order_book_clear(book_);
    }
    
    void example_fok() {
        std::cout << "\n=== FOK (Fill or Kill) Example ===\n";
        
        // Setup
        mx_order_book_add_limit(book_, 100, MX_SIDE_SELL, 5000000, 30);
        mx_order_book_add_limit(book_, 101, MX_SIDE_SELL, 5010000, 30);
        
        std::cout << "\nFOK Buy 100 @ $50.1k (should REJECT - insufficient):\n";
        mx_order_book_add_order(book_, 200, MX_ORDER_TYPE_LIMIT, MX_SIDE_BUY,
                               5010000, 0, 100, 0, MX_TIF_FOK, MX_ORDER_FLAG_NONE, 0);
        
        std::cout << "\nFOK Buy 50 @ $50.1k (should FILL completely):\n";
        mx_order_book_add_order(book_, 201, MX_ORDER_TYPE_LIMIT, MX_SIDE_BUY,
                               5010000, 0, 50, 0, MX_TIF_FOK, MX_ORDER_FLAG_NONE, 0);
        
        trades_.clear();
        mx_order_book_clear(book_);
    }
    
    void example_iceberg() {
        std::cout << "\n=== Iceberg (Hidden Liquidity) Example ===\n";
        
        std::cout << "\nAdding iceberg sell: 500 total, 100 visible @ $50k:\n";
        mx_order_book_add_order(book_, 100, MX_ORDER_TYPE_LIMIT, MX_SIDE_SELL,
                               5000000, 0, 500, 100, MX_TIF_GTC, 
                               MX_ORDER_FLAG_HIDDEN, 0);
        
        std::cout << "\nMatching 100 shares:\n";
        mx_order_book_add_limit(book_, 200, MX_SIDE_BUY, 5000000, 100);
        
        std::cout << "\nMatching another 100 (should refresh visible portion):\n";
        mx_order_book_add_limit(book_, 201, MX_SIDE_BUY, 5000000, 100);
        
        std::cout << "\nIceberg order still has 300 shares remaining\n";
        
        trades_.clear();
        mx_order_book_clear(book_);
    }
    
    void example_post_only() {
        std::cout << "\n=== Post-Only (Maker-Only) Example ===\n";
        
        // Setup
        mx_order_book_add_limit(book_, 100, MX_SIDE_SELL, 5000000, 50);
        
        std::cout << "\nPost-only buy @ $50k (should REJECT - would match):\n";
        mx_order_book_add_order(book_, 200, MX_ORDER_TYPE_LIMIT, MX_SIDE_BUY,
                               5000000, 0, 50, 0, MX_TIF_GTC, 
                               MX_ORDER_FLAG_POST_ONLY, 0);
        
        std::cout << "\nPost-only buy @ $49.9k (should ACCEPT - won't match):\n";
        mx_order_book_add_order(book_, 201, MX_ORDER_TYPE_LIMIT, MX_SIDE_BUY,
                               4990000, 0, 50, 0, MX_TIF_GTC,
                               MX_ORDER_FLAG_POST_ONLY, 0);
        
        print_market_data();
        
        trades_.clear();
        mx_order_book_clear(book_);
    }
    
    void example_modify() {
        std::cout << "\n=== Order Modification Example ===\n";
        
        std::cout << "\nAdding buy order: 100 shares @ $49.5k:\n";
        mx_order_book_add_limit(book_, 100, MX_SIDE_BUY, 4950000, 100);
        
        std::cout << "\nReducing to 50 shares (maintains time priority):\n";
        int result = mx_order_book_modify(book_, 100, 50);
        std::cout << "  Modify result: " << mx_status_message((mx_status_t)result) << "\n";
        
        // Verify
        uint32_t qty = mx_order_book_get_volume_at_price(book_, MX_SIDE_BUY, 4950000);
        std::cout << "  Volume at $49.5k: " << qty << " shares\n";
        
        mx_order_book_clear(book_);
    }
    
    void run_all_examples() {
        std::cout << "MatchX Advanced Usage Examples\n";
        std::cout << "==============================\n";
        
        example_ioc();
        example_fok();
        example_iceberg();
        example_post_only();
        example_modify();
        
        std::cout << "\n✓ All examples complete!\n";
        std::cout << "\nTotal trades executed: " << trades_.size() << "\n";
    }
};

int main() {
    try {
        TradingSimulator sim;
        sim.run_all_examples();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
