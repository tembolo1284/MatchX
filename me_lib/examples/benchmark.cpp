/**
 * Benchmark - Performance testing
 * Measures throughput and latency
 */

#include "matchengine.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>
#include <cstdlib>

using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double>;

class Benchmark {
private:
    mx_context_t* ctx_;
    mx_order_book_t* book_;
    size_t trade_count_;
    
    static void trade_callback(void* user_data, uint64_t, uint64_t,
                              uint32_t, uint32_t, uint64_t) {
        auto* bench = static_cast<Benchmark*>(user_data);
        bench->trade_count_++;
    }

public:
    Benchmark() : trade_count_(0) {
        ctx_ = mx_context_new();
        mx_context_set_callbacks(ctx_, trade_callback, nullptr, this);
        book_ = mx_order_book_new(ctx_, "BENCH");
    }
    
    ~Benchmark() {
        mx_order_book_free(book_);
        mx_context_free(ctx_);
    }
    
    void bench_add_orders(size_t count) {
        std::cout << "\nBenchmark: Add " << count << " orders\n";
        std::cout << std::string(50, '-') << "\n";
        
        mx_order_book_clear(book_);
        
        auto start = Clock::now();
        
        for (size_t i = 0; i < count; ++i) {
            mx_side_t side = (i % 2 == 0) ? MX_SIDE_BUY : MX_SIDE_SELL;
            uint32_t price = 10000000 + (i % 100) * 100;  // $100k +/- $0.01
            
            mx_order_book_add_limit(book_, i + 1, side, price, 100);
        }
        
        auto end = Clock::now();
        Duration elapsed = end - start;
        
        double orders_per_sec = count / elapsed.count();
        double ns_per_order = (elapsed.count() * 1e9) / count;
        
        std::cout << "  Time:         " << std::fixed << std::setprecision(4) 
                  << elapsed.count() << " seconds\n";
        std::cout << "  Orders/sec:   " << std::fixed << std::setprecision(0) 
                  << orders_per_sec << "\n";
        std::cout << "  Latency:      " << std::fixed << std::setprecision(0) 
                  << ns_per_order << " ns/order\n";
    }
    
    void bench_cancel_orders(size_t count) {
        std::cout << "\nBenchmark: Cancel " << count << " orders\n";
        std::cout << std::string(50, '-') << "\n";
        
        mx_order_book_clear(book_);
        
        // Add orders first
        for (size_t i = 0; i < count; ++i) {
            mx_order_book_add_limit(book_, i + 1, MX_SIDE_BUY, 10000000, 100);
        }
        
        // Benchmark cancellation
        auto start = Clock::now();
        
        for (size_t i = 0; i < count; ++i) {
            mx_order_book_cancel(book_, i + 1);
        }
        
        auto end = Clock::now();
        Duration elapsed = end - start;
        
        double cancels_per_sec = count / elapsed.count();
        double ns_per_cancel = (elapsed.count() * 1e9) / count;
        
        std::cout << "  Time:         " << std::fixed << std::setprecision(4) 
                  << elapsed.count() << " seconds\n";
        std::cout << "  Cancels/sec:  " << std::fixed << std::setprecision(0) 
                  << cancels_per_sec << "\n";
        std::cout << "  Latency:      " << std::fixed << std::setprecision(0) 
                  << ns_per_cancel << " ns/cancel\n";
    }
    
    void bench_matching(size_t count) {
        std::cout << "\nBenchmark: Match " << count << " orders\n";
        std::cout << std::string(50, '-') << "\n";
        
        mx_order_book_clear(book_);
        trade_count_ = 0;
        
        // Add passive orders
        for (size_t i = 0; i < count; ++i) {
            mx_order_book_add_limit(book_, i + 1, MX_SIDE_SELL, 10000000, 10);
        }
        
        // Benchmark aggressive matching
        auto start = Clock::now();
        
        for (size_t i = 0; i < count; ++i) {
            mx_order_book_add_limit(book_, count + i + 1, MX_SIDE_BUY, 10000000, 10);
        }
        
        auto end = Clock::now();
        Duration elapsed = end - start;
        
        double matches_per_sec = count / elapsed.count();
        double ns_per_match = (elapsed.count() * 1e9) / count;
        
        std::cout << "  Time:         " << std::fixed << std::setprecision(4) 
                  << elapsed.count() << " seconds\n";
        std::cout << "  Matches/sec:  " << std::fixed << std::setprecision(0) 
                  << matches_per_sec << "\n";
        std::cout << "  Latency:      " << std::fixed << std::setprecision(0) 
                  << ns_per_match << " ns/match\n";
        std::cout << "  Trades:       " << trade_count_ << "\n";
    }
    
    void bench_queries(size_t count) {
        std::cout << "\nBenchmark: " << count << " market data queries\n";
        std::cout << std::string(50, '-') << "\n";
        
        mx_order_book_clear(book_);
        
        // Setup book
        for (size_t i = 0; i < 100; ++i) {
            mx_order_book_add_limit(book_, i + 1, MX_SIDE_BUY, 
                                   9900000 + i * 100, 100);
            mx_order_book_add_limit(book_, i + 1001, MX_SIDE_SELL,
                                   10100000 + i * 100, 100);
        }
        
        // Benchmark queries
        auto start = Clock::now();
        
        uint64_t checksum = 0;
        for (size_t i = 0; i < count; ++i) {
            checksum += mx_order_book_get_best_bid(book_);
            checksum += mx_order_book_get_best_ask(book_);
            checksum += mx_order_book_get_spread(book_);
        }
        
        auto end = Clock::now();
        Duration elapsed = end - start;
        
        double queries_per_sec = (count * 3) / elapsed.count();
        double ns_per_query = (elapsed.count() * 1e9) / (count * 3);
        
        std::cout << "  Time:         " << std::fixed << std::setprecision(4) 
                  << elapsed.count() << " seconds\n";
        std::cout << "  Queries/sec:  " << std::fixed << std::setprecision(0) 
                  << queries_per_sec << "\n";
        std::cout << "  Latency:      " << std::fixed << std::setprecision(0) 
                  << ns_per_query << " ns/query\n";
        std::cout << "  (checksum: " << checksum << ")\n";
    }
    
    void run_all_benchmarks() {
        std::cout << "\n";
        std::cout << "╔════════════════════════════════════════════════╗\n";
        std::cout << "║   MatchX Performance Benchmark                 ║\n";
        std::cout << "╚════════════════════════════════════════════════╝\n";
        
        bench_add_orders(10000);
        bench_cancel_orders(10000);
        bench_matching(5000);
        bench_queries(100000);
        
        std::cout << "\n✓ Benchmark complete!\n\n";
    }
};

int main() {
    std::cout << "MatchX Matching Engine - Performance Benchmark\n";
    std::cout << "===============================================\n";
    
    // Version check
    unsigned int version = mx_get_version();
    std::cout << "Library version: " 
              << ((version >> 16) & 0xFF) << "."
              << ((version >> 8) & 0xFF) << "."
              << (version & 0xFF) << "\n";
    
    Benchmark bench;
    bench.run_all_benchmarks();
    
    return 0;
}
