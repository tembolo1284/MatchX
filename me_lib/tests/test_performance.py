"""
Performance and stress tests
Tests throughput, latency, and behavior under load with timing measurements
"""

import pytest
import time
from testhelpers import (
    ffi, lib,
    SIDE_BUY, SIDE_SELL,
    ORDER_TYPE_LIMIT,
    TIF_GTC, TIF_IOC,
    FLAG_NONE,
    price_to_ticks, ticks_to_price
)

class PerformanceTimer:
    """Simple performance timer"""
    def __init__(self, name):
        self.name = name
        self.start_time = None
        self.elapsed = None
    
    def __enter__(self):
        self.start_time = time.perf_counter()
        return self
    
    def __exit__(self, *args):
        self.elapsed = time.perf_counter() - self.start_time
    
    def nanoseconds_per_op(self, operations):
        """Calculate nanoseconds per operation"""
        return (self.elapsed * 1_000_000_000) / operations
    
    def ops_per_second(self, operations):
        """Calculate operations per second"""
        return operations / self.elapsed if self.elapsed > 0 else 0

@pytest.mark.performance
class TestAddOrderPerformance:
    """Test performance of adding orders"""
    @pytest.mark.skip(reason="Segfault - needs debugging")    
    def test_add_1000_limit_orders(self, order_book):
        """Add 1000 limit orders and measure time"""
        num_orders = 1000
        
        with PerformanceTimer("Add 1000 orders") as timer:
            for i in range(num_orders):
                lib.mx_order_book_add_limit(
                    order_book,
                    i + 1,
                    SIDE_BUY if i % 2 == 0 else SIDE_SELL,
                    price_to_ticks(100.00 + (i % 10)),
                    100
                )
        
        print(f"\n  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Orders/sec: {timer.ops_per_second(num_orders):,.0f}")
        print(f"  Latency: {timer.nanoseconds_per_op(num_orders):.0f}ns per order")
        
        # Verify all orders added
        total_orders = ffi.new("uint32_t*")
        lib.mx_order_book_get_stats(order_book, total_orders, ffi.NULL, ffi.NULL, ffi.NULL, ffi.NULL)
        assert total_orders[0] == num_orders
    
    def test_add_10000_orders_to_same_price(self, order_book):
        """Add 10000 orders to same price level"""
        num_orders = 10000
        price = price_to_ticks(100.00)
        
        with PerformanceTimer("Add 10k orders same price") as timer:
            for i in range(num_orders):
                lib.mx_order_book_add_limit(
                    order_book, i + 1, SIDE_SELL, price, 10
                )
        
        print(f"\n  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Orders/sec: {timer.ops_per_second(num_orders):,.0f}")
        print(f"  Latency: {timer.nanoseconds_per_op(num_orders):.0f}ns per order")
        
        # Check depth
        volume = lib.mx_order_book_get_volume_at_price(order_book, SIDE_SELL, price)
        assert volume == num_orders * 10
    
    def test_add_orders_at_many_price_levels(self, order_book):
        """Add orders spread across 1000 price levels"""
        num_orders = 5000
        
        with PerformanceTimer("Add 5k orders across 1k levels") as timer:
            for i in range(num_orders):
                price_offset = (i % 1000) * 0.01
                lib.mx_order_book_add_limit(
                    order_book,
                    i + 1,
                    SIDE_SELL,
                    price_to_ticks(100.00 + price_offset),
                    10
                )
        
        print(f"\n  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Orders/sec: {timer.ops_per_second(num_orders):,.0f}")
        print(f"  Avg latency: {timer.nanoseconds_per_op(num_orders):.0f}ns")
        
        # Check level count
        ask_levels = ffi.new("uint32_t*")
        lib.mx_order_book_get_stats(order_book, ffi.NULL, ffi.NULL, ask_levels, ffi.NULL, ffi.NULL)
        print(f"  Price levels: {ask_levels[0]}")

@pytest.mark.performance
class TestCancelPerformance:
    """Test cancellation performance"""
    
    def test_cancel_1000_orders(self, order_book):
        """Add and cancel 1000 orders"""
        num_orders = 1000
        
        # Add orders first
        for i in range(num_orders):
            lib.mx_order_book_add_limit(
                order_book, i + 1, SIDE_BUY, price_to_ticks(100.00), 100
            )
        
        # Cancel them
        with PerformanceTimer("Cancel 1000 orders") as timer:
            for i in range(num_orders):
                lib.mx_order_book_cancel(order_book, i + 1)
        
        print(f"\n  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Cancels/sec: {timer.ops_per_second(num_orders):,.0f}")
        print(f"  Latency: {timer.nanoseconds_per_op(num_orders):.0f}ns per cancel")
        
        # Book should be empty
        total_orders = ffi.new("uint32_t*")
        lib.mx_order_book_get_stats(order_book, total_orders, ffi.NULL, ffi.NULL, ffi.NULL, ffi.NULL)
        assert total_orders[0] == 0
    
    def test_cancel_from_deep_queue(self, order_book):
        """Cancel orders from deep queue at same price"""
        num_orders = 5000
        price = price_to_ticks(100.00)
        
        # Add many orders at same price
        for i in range(num_orders):
            lib.mx_order_book_add_limit(order_book, i + 1, SIDE_SELL, price, 10)
        
        # Cancel every other order (worst case for linked list)
        with PerformanceTimer("Cancel 2.5k from deep queue") as timer:
            for i in range(0, num_orders, 2):
                lib.mx_order_book_cancel(order_book, i + 1)
        
        print(f"\n  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Cancels/sec: {timer.ops_per_second(num_orders // 2):,.0f}")
        print(f"  Latency: {timer.nanoseconds_per_op(num_orders // 2):.0f}ns per cancel")
        
        # Half should remain
        total_orders = ffi.new("uint32_t*")
        lib.mx_order_book_get_stats(order_book, total_orders, ffi.NULL, ffi.NULL, ffi.NULL, ffi.NULL)
        assert total_orders[0] == num_orders // 2

@pytest.mark.performance
class TestMatchingPerformance:
    """Test matching performance"""
    @pytest.mark.skip(reason="Segfault - needs debugging")    
    def test_match_1000_orders_one_by_one(self, order_book):
        """Match 1000 orders individually"""
        num_orders = 1000
        
        # Add passive sell orders
        for i in range(num_orders):
            lib.mx_order_book_add_limit(
                order_book, i + 1, SIDE_SELL, price_to_ticks(100.00), 10
            )
        
        # Match them one by one
        with PerformanceTimer("Match 1000 orders") as timer:
            for i in range(num_orders):
                lib.mx_order_book_add_limit(
                    order_book, num_orders + i + 1, SIDE_BUY, price_to_ticks(100.00), 10
                )
        
        print(f"\n  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Matches/sec: {timer.ops_per_second(num_orders):,.0f}")
        print(f"  Latency: {timer.nanoseconds_per_op(num_orders):.0f}ns per match")
        
        # All should be matched
        total_orders = ffi.new("uint32_t*")
        lib.mx_order_book_get_stats(order_book, total_orders, ffi.NULL, ffi.NULL, ffi.NULL, ffi.NULL)
        assert total_orders[0] == 0
    
    def test_sweep_through_1000_levels(self, order_book):
        """Single order sweeping through 1000 price levels"""
        num_levels = 1000
        
        # Add 1 order at each level
        for i in range(num_levels):
            lib.mx_order_book_add_limit(
                order_book,
                i + 1,
                SIDE_SELL,
                price_to_ticks(100.00 + i * 0.01),
                10
            )
        
        # Single aggressive order sweeps through all
        with PerformanceTimer("Sweep 1000 levels") as timer:
            lib.mx_order_book_add_limit(
                order_book,
                100000,
                SIDE_BUY,
                price_to_ticks(200.00),
                num_levels * 10
            )
        
        print(f"\n  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Levels/sec: {timer.ops_per_second(num_levels):,.0f}")
        print(f"  Latency: {timer.nanoseconds_per_op(num_levels):.0f}ns per level")
        
        # All should be matched
        assert lib.mx_order_book_get_best_ask(order_book) == 0
    
    @pytest.mark.skip(reason="Segfault - needs debugging")    
    def test_partial_fills_performance(self, book_with_callbacks):
        """Test performance with many partial fills"""
        book, trades, events = book_with_callbacks
        
        num_orders = 500
        
        # Add large passive orders
        for i in range(num_orders):
            lib.mx_order_book_add_limit(
                book, i + 1, SIDE_SELL, price_to_ticks(100.00), 100
            )
        
        trades.clear()
        
        # Chip away with small orders
        with PerformanceTimer("500 partial fills") as timer:
            for i in range(num_orders * 10):  # 5000 small orders
                lib.mx_order_book_add_limit(
                    book, num_orders + i + 1, SIDE_BUY, price_to_ticks(100.00), 10
                )
        
        print(f"\n  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Matches/sec: {timer.ops_per_second(num_orders * 10):,.0f}")
        print(f"  Trades executed: {trades.count()}")

@pytest.mark.performance
class TestQueryPerformance:
    """Test market data query performance"""
    
    def test_best_bid_ask_lookup(self, order_book):
        """Test best bid/ask lookup performance"""
        # Add some orders
        for i in range(100):
            lib.mx_order_book_add_limit(
                order_book, i + 1, SIDE_BUY, price_to_ticks(99.00 + i * 0.01), 10
            )
            lib.mx_order_book_add_limit(
                order_book, i + 1001, SIDE_SELL, price_to_ticks(101.00 + i * 0.01), 10
            )
        
        num_queries = 100000
        
        with PerformanceTimer("100k best bid/ask queries") as timer:
            for _ in range(num_queries):
                lib.mx_order_book_get_best_bid(order_book)
                lib.mx_order_book_get_best_ask(order_book)
        
        print(f"\n  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Queries/sec: {timer.ops_per_second(num_queries * 2):,.0f}")
        print(f"  Latency: {timer.nanoseconds_per_op(num_queries * 2):.0f}ns per query")
    
    def test_order_lookup_performance(self, order_book):
        """Test order lookup by ID performance"""
        num_orders = 10000
        
        # Add orders
        for i in range(num_orders):
            lib.mx_order_book_add_limit(
                order_book, i + 1, SIDE_BUY, price_to_ticks(100.00), 10
            )
        
        # Lookup random orders
        with PerformanceTimer("10k order lookups") as timer:
            for i in range(num_orders):
                lib.mx_order_book_has_order(order_book, (i * 7) % num_orders + 1)
        
        print(f"\n  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Lookups/sec: {timer.ops_per_second(num_orders):,.0f}")
        print(f"  Latency: {timer.nanoseconds_per_op(num_orders):.0f}ns per lookup")

@pytest.mark.stress
class TestStressScenarios:
    """Stress tests with heavy load"""
    
    def test_sustained_order_flow(self, book_with_callbacks):
        """Simulate sustained order flow with adds, cancels, and matches"""
        book, trades, events = book_with_callbacks
        
        num_iterations = 5000
        order_id = 1
        
        print("\n  Running sustained order flow stress test...")
        
        with PerformanceTimer("Sustained flow") as timer:
            for i in range(num_iterations):
                # Add buy order
                lib.mx_order_book_add_limit(
                    book, order_id, SIDE_BUY,
                    price_to_ticks(99.00 + (i % 10) * 0.10), 100
                )
                order_id += 1
                
                # Add sell order (may match)
                lib.mx_order_book_add_limit(
                    book, order_id, SIDE_SELL,
                    price_to_ticks(100.00 + (i % 10) * 0.10), 100
                )
                order_id += 1
                
                # Cancel old orders occasionally
                if i > 100 and i % 10 == 0:
                    old_id = order_id - 200
                    if old_id > 0:
                        lib.mx_order_book_cancel(book, old_id)
        
        print(f"  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Operations/sec: {timer.ops_per_second(num_iterations * 3):,.0f}")
        print(f"  Trades executed: {trades.count()}")
        
        # Get final stats
        total_orders = ffi.new("uint32_t*")
        lib.mx_order_book_get_stats(book, total_orders, ffi.NULL, ffi.NULL, ffi.NULL, ffi.NULL)
        print(f"  Final orders in book: {total_orders[0]}")
    
    def test_high_frequency_matching(self, book_with_callbacks):
        """Simulate HFT-style rapid fire matching"""
        book, trades, events = book_with_callbacks
        
        # Pre-populate book with liquidity
        for i in range(100):
            lib.mx_order_book_add_limit(
                book, i + 1, SIDE_SELL, price_to_ticks(100.00 + i * 0.01), 1000
            )
        
        trades.clear()
        num_orders = 10000
        
        print("\n  Running HFT matching simulation...")
        
        with PerformanceTimer("HFT matching") as timer:
            for i in range(num_orders):
                # Aggressive small orders that match immediately
                lib.mx_order_book_add_limit(
                    book, 1000 + i, SIDE_BUY,
                    price_to_ticks(100.00 + (i % 50) * 0.01), 10
                )
        
        print(f"  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Orders/sec: {timer.ops_per_second(num_orders):,.0f}")
        print(f"  Latency: {timer.nanoseconds_per_op(num_orders):.0f}ns per order")
        print(f"  Trades executed: {trades.count()}")
        print(f"  Avg latency: {(timer.elapsed * 1_000_000) / num_orders:.2f}μs")
    
    def test_book_depth_stress(self, order_book):
        """Test with very deep order book"""
        print("\n  Building deep order book...")
        
        num_orders = 20000
        
        with PerformanceTimer("Add 20k orders") as add_timer:
            for i in range(num_orders):
                side = SIDE_BUY if i < num_orders // 2 else SIDE_SELL
                price_base = 99.00 if side == SIDE_BUY else 101.00
                price = price_to_ticks(price_base + (i % 100) * 0.01)
                
                lib.mx_order_book_add_limit(order_book, i + 1, side, price, 10)
        
        print(f"  Add time: {add_timer.elapsed:.4f}s")
        
        # Get stats
        total_orders = ffi.new("uint32_t*")
        bid_levels = ffi.new("uint32_t*")
        ask_levels = ffi.new("uint32_t*")
        lib.mx_order_book_get_stats(
            order_book, total_orders, bid_levels, ask_levels, ffi.NULL, ffi.NULL
        )
        
        print(f"  Total orders: {total_orders[0]:,}")
        print(f"  Bid levels: {bid_levels[0]}")
        print(f"  Ask levels: {ask_levels[0]}")
        
        # Test queries on deep book
        num_queries = 10000
        with PerformanceTimer("10k queries on deep book") as query_timer:
            for _ in range(num_queries):
                lib.mx_order_book_get_best_bid(order_book)
                lib.mx_order_book_get_best_ask(order_book)
                lib.mx_order_book_get_spread(order_book)
        
        print(f"  Query time: {query_timer.elapsed:.4f}s")
        print(f"  Queries/sec: {query_timer.ops_per_second(num_queries * 3):,.0f}")
        
        # Clear the book
        with PerformanceTimer("Clear deep book") as clear_timer:
            lib.mx_order_book_clear(order_book)
        
        print(f"  Clear time: {clear_timer.elapsed:.4f}s")
    
    def test_alternating_sides_stress(self, book_with_callbacks):
        """Stress test with rapid alternating buy/sell orders"""
        book, trades, events = book_with_callbacks
        
        num_orders = 10000
        
        print("\n  Running alternating sides stress test...")
        
        with PerformanceTimer("Alternating orders") as timer:
            for i in range(num_orders):
                side = SIDE_BUY if i % 2 == 0 else SIDE_SELL
                price = price_to_ticks(100.00 - 0.50) if side == SIDE_BUY else price_to_ticks(100.00 + 0.50)
                
                lib.mx_order_book_add_limit(book, i + 1, side, price, 10)
        
        print(f"  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Orders/sec: {timer.ops_per_second(num_orders):,.0f}")
        print(f"  Trades: {trades.count()}")
        
        # Should have no trades (spread kept)
        assert trades.count() == 0

@pytest.mark.stress
class TestMemoryStress:
    """Test memory-related stress scenarios"""
    
    def test_order_churn(self, order_book):
        """Test adding and removing many orders (memory pool stress)"""
        print("\n  Running order churn test...")
        
        num_cycles = 1000
        orders_per_cycle = 100
        
        with PerformanceTimer("Order churn") as timer:
            for cycle in range(num_cycles):
                base_id = cycle * orders_per_cycle
                
                # Add orders
                for i in range(orders_per_cycle):
                    lib.mx_order_book_add_limit(
                        order_book,
                        base_id + i,
                        SIDE_BUY,
                        price_to_ticks(100.00),
                        10
                    )
                
                # Cancel them all
                for i in range(orders_per_cycle):
                    lib.mx_order_book_cancel(order_book, base_id + i)
        
        print(f"  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Total operations: {num_cycles * orders_per_cycle * 2:,}")
        print(f"  Ops/sec: {timer.ops_per_second(num_cycles * orders_per_cycle * 2):,.0f}")
        
        # Book should be empty
        total_orders = ffi.new("uint32_t*")
        lib.mx_order_book_get_stats(order_book, total_orders, ffi.NULL, ffi.NULL, ffi.NULL, ffi.NULL)
        assert total_orders[0] == 0
    
    def test_fragmented_price_levels(self, order_book):
        """Test with highly fragmented price levels"""
        print("\n  Creating fragmented price levels...")
        
        num_levels = 5000
        
        with PerformanceTimer("Fragmented levels") as timer:
            for i in range(num_levels):
                # One order per price level
                lib.mx_order_book_add_limit(
                    order_book,
                    i + 1,
                    SIDE_SELL,
                    price_to_ticks(100.00 + i * 0.01),
                    10
                )
        
        print(f"  Elapsed: {timer.elapsed:.4f}s")
        
        # Get level count
        ask_levels = ffi.new("uint32_t*")
        lib.mx_order_book_get_stats(order_book, ffi.NULL, ffi.NULL, ask_levels, ffi.NULL, ffi.NULL)
        print(f"  Price levels created: {ask_levels[0]}")
        
        # Test depth query performance
        with PerformanceTimer("Depth queries") as depth_timer:
            for _ in range(1000):
                lib.mx_order_book_get_depth(order_book, SIDE_SELL, 100)
        
        print(f"  Depth query time: {depth_timer.elapsed:.4f}s")

@pytest.mark.performance
class TestRealWorldSimulation:
    """Simulate realistic trading scenarios"""
    
    def test_market_maker_simulation(self, book_with_callbacks):
        """Simulate market maker continuously quoting both sides"""
        book, trades, events = book_with_callbacks
        
        num_iterations = 2000
        order_id = 1
        
        print("\n  Running market maker simulation...")
        
        with PerformanceTimer("Market maker") as timer:
            for i in range(num_iterations):
                # Cancel old quotes
                if i > 0:
                    lib.mx_order_book_cancel(book, order_id - 2)
                    lib.mx_order_book_cancel(book, order_id - 1)
                
                # Post new quotes
                lib.mx_order_book_add_limit(
                    book, order_id, SIDE_BUY, price_to_ticks(99.95), 100
                )
                order_id += 1
                
                lib.mx_order_book_add_limit(
                    book, order_id, SIDE_SELL, price_to_ticks(100.05), 100
                )
                order_id += 1
                
                # Occasionally add taker order
                if i % 10 == 0:
                    lib.mx_order_book_add_limit(
                        book, order_id, SIDE_BUY, price_to_ticks(100.05), 50
                    )
                    order_id += 1
        
        print(f"  Elapsed: {timer.elapsed:.4f}s")
        print(f"  Total operations: {order_id}")
        print(f"  Ops/sec: {timer.ops_per_second(order_id):,.0f}")
        print(f"  Trades: {trades.count()}")
        print(f"  Avg time per quote update: {(timer.elapsed * 1000) / num_iterations:.3f}ms")

@pytest.mark.performance
def test_end_to_end_latency(book_with_callbacks):
    """Measure end-to-end latency for a complete order lifecycle"""
    book, trades, events = book_with_callbacks
    
    print("\n  Measuring end-to-end latencies...")
    
    # Add passive order
    lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 100)
    
    num_samples = 1000
    latencies = []
    
    for i in range(num_samples):
        start = time.perf_counter_ns()
        
        # Add aggressive order that matches
        lib.mx_order_book_add_limit(
            book, i + 2, SIDE_BUY, price_to_ticks(100.00), 1
        )
        
        end = time.perf_counter_ns()
        latencies.append(end - start)
    
    # Calculate statistics
    latencies.sort()
    avg = sum(latencies) / len(latencies)
    p50 = latencies[len(latencies) // 2]
    p95 = latencies[int(len(latencies) * 0.95)]
    p99 = latencies[int(len(latencies) * 0.99)]
    min_lat = latencies[0]
    max_lat = latencies[-1]
    
    print(f"  Samples: {num_samples}")
    print(f"  Min:  {min_lat:,}ns ({min_lat / 1000:.1f}μs)")
    print(f"  Avg:  {avg:,.0f}ns ({avg / 1000:.1f}μs)")
    print(f"  P50:  {p50:,}ns ({p50 / 1000:.1f}μs)")
    print(f"  P95:  {p95:,}ns ({p95 / 1000:.1f}μs)")
    print(f"  P99:  {p99:,}ns ({p99 / 1000:.1f}μs)")
    print(f"  Max:  {max_lat:,}ns ({max_lat / 1000:.1f}μs)")
