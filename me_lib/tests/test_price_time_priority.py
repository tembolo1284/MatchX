"""
Price-time priority tests
Rigorous testing of price-time priority rules and order queue behavior
"""

import pytest
from testhelpers import (
    ffi, lib,
    SIDE_BUY, SIDE_SELL,
    price_to_ticks, ticks_to_price
)

class TestStrictTimePriority:
    """Test strict time priority at same price level"""
    
    def test_fifo_order_at_same_price(self, book_with_callbacks):
        """Test strict FIFO ordering at same price"""
        book, trades, events = book_with_callbacks
        
        # Add 5 sell orders at exact same price
        for i in range(1, 6):
            lib.mx_order_book_add_limit(
                book, i, SIDE_SELL, price_to_ticks(100.00), 10
            )
        
        trades.clear()
        
        # Match with exact quantity to trigger all 5 in sequence
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(100.00), 50)
        
        # Should have 5 trades in exact time order
        assert trades.count() == 5
        
        all_trades = trades.get_all()
        for i, trade in enumerate(all_trades):
            assert trade['passive_id'] == i + 1  # Order IDs 1, 2, 3, 4, 5
            assert trade['quantity'] == 10
    
    def test_later_orders_wait_their_turn(self, book_with_callbacks):
        """Test that later orders don't jump the queue"""
        book, trades, events = book_with_callbacks
        
        # Add orders at same price with timestamps
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 100)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 100)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(100.00), 100)
        
        trades.clear()
        
        # Match 150 shares - should fill order 1 completely and order 2 partially
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(100.00), 150)
        
        # Order 1 should be gone
        assert lib.mx_order_book_has_order(book, 1) == 0
        
        # Order 2 should have 50 remaining
        qty_out = ffi.new("uint32_t*")
        lib.mx_order_book_get_order_info(book, 2, ffi.NULL, ffi.NULL, qty_out, ffi.NULL)
        assert qty_out[0] == 50
        
        # Order 3 should be untouched
        lib.mx_order_book_get_order_info(book, 3, ffi.NULL, ffi.NULL, qty_out, ffi.NULL)
        assert qty_out[0] == 100
        
        trades.clear()
        
        # Match another 100 - should finish order 2, then take 50 from order 3
        lib.mx_order_book_add_limit(book, 101, SIDE_BUY, price_to_ticks(100.00), 100)
        
        all_trades = trades.get_all()
        assert all_trades[0]['passive_id'] == 2
        assert all_trades[0]['quantity'] == 50
        assert all_trades[1]['passive_id'] == 3
        assert all_trades[1]['quantity'] == 50
    
    def test_cancel_does_not_affect_time_priority(self, book_with_callbacks):
        """Test that cancelling one order doesn't affect others' priority"""
        book, trades, events = book_with_callbacks
        
        # Add three orders
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 50)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 50)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(100.00), 50)
        
        # Cancel middle order
        lib.mx_order_book_cancel(book, 2)
        
        trades.clear()
        
        # Match - should go to order 1 first, then order 3
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(100.00), 100)
        
        all_trades = trades.get_all()
        assert all_trades[0]['passive_id'] == 1
        assert all_trades[1]['passive_id'] == 3  # Not order 2!

class TestStrictPricePriority:
    """Test strict price priority"""
    
    def test_best_price_always_matches_first(self, book_with_callbacks):
        """Test that best price always gets matched first"""
        book, trades, events = book_with_callbacks
        
        # Add sells at different prices (add in random order)
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(102.00), 50)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 50)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(101.00), 50)
        
        trades.clear()
        
        # Buy at 102 - should match order 2 first (lowest price)
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(102.00), 50)
        
        assert trades.count() == 1
        assert trades.get_last()['passive_id'] == 2
        assert trades.get_last()['price'] == price_to_ticks(100.00)
    
    def test_price_priority_overrides_time(self, book_with_callbacks):
        """Test that better price beats earlier time"""
        book, trades, events = book_with_callbacks
        
        # Add older order at worse price
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(101.00), 50)
        
        # Add newer order at better price
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 50)
        
        trades.clear()
        
        # Should match order 2 despite being added later
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(101.00), 50)
        
        assert trades.get_last()['passive_id'] == 2
    
    def test_walk_through_price_levels_in_order(self, book_with_callbacks):
        """Test that matching walks through price levels correctly"""
        book, trades, events = book_with_callbacks
        
        # Build a deep order book
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 20)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.50), 20)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(101.00), 20)
        lib.mx_order_book_add_limit(book, 4, SIDE_SELL, price_to_ticks(101.50), 20)
        lib.mx_order_book_add_limit(book, 5, SIDE_SELL, price_to_ticks(102.00), 20)
        
        trades.clear()
        
        # Large buy walks through all levels
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(103.00), 100)
        
        # Should match in exact price order
        assert trades.count() == 5
        all_trades = trades.get_all()
        
        assert all_trades[0]['price'] == price_to_ticks(100.00)
        assert all_trades[1]['price'] == price_to_ticks(100.50)
        assert all_trades[2]['price'] == price_to_ticks(101.00)
        assert all_trades[3]['price'] == price_to_ticks(101.50)
        assert all_trades[4]['price'] == price_to_ticks(102.00)

class TestPriceTimeCombinations:
    """Test complex price-time priority scenarios"""
    
    def test_multiple_orders_at_multiple_prices(self, book_with_callbacks):
        """Test matching with multiple orders at multiple price levels"""
        book, trades, events = book_with_callbacks
        
        # Price level $100: orders 1, 2, 3
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 30)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 30)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(100.00), 30)
        
        # Price level $101: orders 4, 5
        lib.mx_order_book_add_limit(book, 4, SIDE_SELL, price_to_ticks(101.00), 30)
        lib.mx_order_book_add_limit(book, 5, SIDE_SELL, price_to_ticks(101.00), 30)
        
        trades.clear()
        
        # Match 100 shares - should take all of $100 level, then 10 from $101
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(101.00), 100)
        
        all_trades = trades.get_all()
        
        # First 3 trades at $100 in time order
        assert all_trades[0]['passive_id'] == 1
        assert all_trades[1]['passive_id'] == 2
        assert all_trades[2]['passive_id'] == 3
        assert all_trades[0]['price'] == price_to_ticks(100.00)
        
        # 4th trade at $101 with order 4
        assert all_trades[3]['passive_id'] == 4
        assert all_trades[3]['price'] == price_to_ticks(101.00)
    
    def test_interleaved_price_levels(self, book_with_callbacks):
        """Test that orders maintain priority when prices are added in mixed order"""
        book, trades, events = book_with_callbacks
        
        # Add in mixed order: $101, $100, $101, $100
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(101.00), 25)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 25)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(101.00), 25)
        lib.mx_order_book_add_limit(book, 4, SIDE_SELL, price_to_ticks(100.00), 25)
        
        trades.clear()
        
        # Match 75 shares
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(101.00), 75)
        
        # Should match: all of $100 level (order 2, then 4), then part of $101
        all_trades = trades.get_all()
        
        assert all_trades[0]['passive_id'] == 2  # First $100 order
        assert all_trades[1]['passive_id'] == 4  # Second $100 order
        assert all_trades[2]['passive_id'] == 1  # First $101 order
        
        assert all_trades[2]['quantity'] == 25  # Remaining quantity

class TestPartialFillPriority:
    """Test that partial fills maintain time priority"""
    
    def test_partial_fill_keeps_priority(self, book_with_callbacks):
        """Test that partially filled order stays at front of queue"""
        book, trades, events = book_with_callbacks
        
        # Add three orders at same price
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 100)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 100)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(100.00), 100)
        
        trades.clear()
        
        # Partially fill order 1
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(100.00), 50)
        
        assert trades.count() == 1
        assert trades.get_last()['passive_id'] == 1
        
        trades.clear()
        
        # Next match should still be order 1 (remaining 50)
        lib.mx_order_book_add_limit(book, 101, SIDE_BUY, price_to_ticks(100.00), 50)
        
        assert trades.count() == 1
        assert trades.get_last()['passive_id'] == 1
        
        # Order 1 should now be gone
        assert lib.mx_order_book_has_order(book, 1) == 0
        
        trades.clear()
        
        # Next match should be order 2
        lib.mx_order_book_add_limit(book, 102, SIDE_BUY, price_to_ticks(100.00), 25)
        
        assert trades.get_last()['passive_id'] == 2
    
    def test_multiple_partial_fills_maintain_priority(self, book_with_callbacks):
        """Test order getting chipped away maintains priority"""
        book, trades, events = book_with_callbacks
        
        # Add orders
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 200)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 100)
        
        trades.clear()
        
        # Chip away at order 1 in small pieces
        for i in range(4):
            lib.mx_order_book_add_limit(book, 100 + i, SIDE_BUY, price_to_ticks(100.00), 40)
        
        # All 4 trades should be with order 1
        all_trades = trades.get_all()
        for trade in all_trades:
            assert trade['passive_id'] == 1
        
        # Order 1 should have 40 remaining (200 - 160)
        qty_out = ffi.new("uint32_t*")
        lib.mx_order_book_get_order_info(book, 1, ffi.NULL, ffi.NULL, qty_out, ffi.NULL)
        assert qty_out[0] == 40
        
        # Order 2 should be untouched
        lib.mx_order_book_get_order_info(book, 2, ffi.NULL, ffi.NULL, qty_out, ffi.NULL)
        assert qty_out[0] == 100

class TestModifyPreservesPriority:
    """Test that quantity reductions preserve time priority"""
    
    def test_reduce_quantity_keeps_priority(self, book_with_callbacks):
        """Test that reducing quantity doesn't lose time priority"""
        book, trades, events = book_with_callbacks
        
        # Add three orders
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 100)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 100)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(100.00), 100)
        
        # Reduce first order's quantity
        result = lib.mx_order_book_modify(book, 1, 50)
        assert result == 0  # Success
        
        trades.clear()
        
        # Match - should still match order 1 first
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(100.00), 50)
        
        assert trades.count() == 1
        assert trades.get_last()['passive_id'] == 1
        
        # Order 1 should be completely filled now
        assert lib.mx_order_book_has_order(book, 1) == 0
    
    def test_modify_between_other_orders(self, book_with_callbacks):
        """Test modifying an order between other orders at same price"""
        book, trades, events = book_with_callbacks
        
        # Add orders: 100, 200, 100 shares
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 100)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 200)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(100.00), 100)
        
        # Reduce middle order
        lib.mx_order_book_modify(book, 2, 50)
        
        trades.clear()
        
        # Match 150 shares - should be order 1 (100), then order 2 (50)
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(100.00), 150)
        
        all_trades = trades.get_all()
        assert all_trades[0]['passive_id'] == 1
        assert all_trades[0]['quantity'] == 100
        assert all_trades[1]['passive_id'] == 2
        assert all_trades[1]['quantity'] == 50
        
        # Order 3 should still have 100
        qty_out = ffi.new("uint32_t*")
        lib.mx_order_book_get_order_info(book, 3, ffi.NULL, ffi.NULL, qty_out, ffi.NULL)
        assert qty_out[0] == 100

class TestAggressiveOrderPriority:
    """Test priority rules for aggressive orders"""
    
    def test_aggressive_order_gets_best_price(self, book_with_callbacks):
        """Test that aggressive order matches at passive order's price"""
        book, trades, events = book_with_callbacks
        
        # Passive sell at $100
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 50)
        
        trades.clear()
        
        # Aggressive buy at $105 - should execute at $100 (seller's price)
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(105.00), 50)
        
        assert trades.count() == 1
        assert trades.get_last()['price'] == price_to_ticks(100.00)  # Passive price
    
    def test_aggressive_gets_price_improvement(self, book_with_callbacks):
        """Test that aggressive order benefits from better passive prices"""
        book, trades, events = book_with_callbacks
        
        # Multiple passive sells
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 30)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.50), 30)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(101.00), 30)
        
        trades.clear()
        
        # Aggressive buy willing to pay $102
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(102.00), 90)
        
        # Should get price improvement at all levels
        all_trades = trades.get_all()
        assert all_trades[0]['price'] == price_to_ticks(100.00)
        assert all_trades[1]['price'] == price_to_ticks(100.50)
        assert all_trades[2]['price'] == price_to_ticks(101.00)
        
        # Never paid $102 even though willing to

class TestComplexScenarios:
    """Test complex real-world scenarios"""
    
    def test_alternating_adds_and_matches(self, book_with_callbacks):
        """Test priority with alternating adds and matches"""
        book, trades, events = book_with_callbacks
        
        # Add order 1
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 50)
        
        # Partially match it
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(100.00), 20)
        
        # Add order 2
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 50)
        
        # Add order 3
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(100.00), 50)
        
        trades.clear()
        
        # Match - should get order 1's remaining 30 first
        lib.mx_order_book_add_limit(book, 101, SIDE_BUY, price_to_ticks(100.00), 100)
        
        all_trades = trades.get_all()
        assert all_trades[0]['passive_id'] == 1
        assert all_trades[0]['quantity'] == 30  # Remaining from order 1
        assert all_trades[1]['passive_id'] == 2
        assert all_trades[2]['passive_id'] == 3
    
    def test_deep_queue_integrity(self, book_with_callbacks):
        """Test priority with deep order queue"""
        book, trades, events = book_with_callbacks
        
        # Add 20 orders at same price
        for i in range(1, 21):
            lib.mx_order_book_add_limit(
                book, i, SIDE_SELL, price_to_ticks(100.00), 10
            )
        
        trades.clear()
        
        # Match them in batches
        lib.mx_order_book_add_limit(book, 100, SIDE_BUY, price_to_ticks(100.00), 100)
        
        # Should match first 10 orders in exact sequence
        all_trades = trades.get_all()
        for i, trade in enumerate(all_trades):
            assert trade['passive_id'] == i + 1
        
        trades.clear()
        
        # Match next batch
        lib.mx_order_book_add_limit(book, 101, SIDE_BUY, price_to_ticks(100.00), 100)
        
        # Should match orders 11-20
        all_trades = trades.get_all()
        for i, trade in enumerate(all_trades):
            assert trade['passive_id'] == i + 11
    
    @pytest.mark.slow
    def test_stress_priority_with_many_operations(self, book_with_callbacks):
        """Stress test with many adds, matches, and cancels"""
        book, trades, events = book_with_callbacks
        
        # Add 100 orders
        for i in range(1, 101):
            lib.mx_order_book_add_limit(
                book, i, SIDE_SELL, price_to_ticks(100.00), 5
            )
        
        # Cancel every 3rd order
        for i in range(3, 101, 3):
            lib.mx_order_book_cancel(book, i)
        
        trades.clear()
        
        # Match 100 shares
        lib.mx_order_book_add_limit(book, 1000, SIDE_BUY, price_to_ticks(100.00), 100)
        
        # Verify all trades are in correct order (skipping cancelled ones)
        all_trades = trades.get_all()
        prev_id = 0
        for trade in all_trades:
            assert trade['passive_id'] > prev_id
            assert trade['passive_id'] % 3 != 0  # None of the cancelled orders
            prev_id = trade['passive_id']
