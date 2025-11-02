"""
Matching logic tests
Tests order matching, price-time priority, partial fills, and trade execution
"""

import pytest
from testhelpers import (
    ffi, lib,
    SIDE_BUY, SIDE_SELL,
    ORDER_TYPE_LIMIT, ORDER_TYPE_MARKET,
    STATUS_OK,
    EVENT_FILLED, EVENT_PARTIAL, EVENT_ACCEPTED,
    price_to_ticks, ticks_to_price
)

class TestBasicMatching:
    """Test basic order matching"""
    
    def test_simple_match(self, book_with_callbacks):
        """Test that matching opposite orders creates a trade"""
        book, trades, events = book_with_callbacks
        
        # Add a sell order at $100
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 50)
        
        # Add a buy order at $100 - should match
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(100.00), 50)
        
        # Should have exactly 1 trade
        assert trades.count() == 1
        
        trade = trades.get_last()
        assert trade['aggressive_id'] == 2  # Buy was aggressive
        assert trade['passive_id'] == 1    # Sell was passive
        assert trade['price'] == price_to_ticks(100.00)
        assert trade['quantity'] == 50
    
    def test_no_match_different_prices(self, book_with_callbacks):
        """Test that orders at different prices don't match"""
        book, trades, events = book_with_callbacks
        
        # Add sell at $101
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(101.00), 50)
        
        # Add buy at $100 - should NOT match
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(100.00), 50)
        
        # No trades should occur
        assert trades.count() == 0
        
        # Both orders should be in book
        assert lib.mx_order_book_has_order(book, 1) == 1
        assert lib.mx_order_book_has_order(book, 2) == 1
    
    def test_match_crosses_spread(self, book_with_callbacks):
        """Test that aggressive order crosses the spread"""
        book, trades, events = book_with_callbacks
        
        # Add sell at $100
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 50)
        
        # Add buy at $102 - should match at sell price ($100)
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(102.00), 50)
        
        # Should match at passive order's price
        assert trades.count() == 1
        trade = trades.get_last()
        assert trade['price'] == price_to_ticks(100.00)  # Seller's price, not buyer's
    
    def test_buy_matches_sell(self, book_with_callbacks):
        """Test buy matching against existing sell"""
        book, trades, events = book_with_callbacks
        
        # Add sell first
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 50)
        trades.clear()  # Clear the acceptance event
        
        # Add matching buy
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(100.00), 50)
        
        assert trades.count() == 1
        assert trades.get_last()['aggressive_id'] == 2
        assert trades.get_last()['passive_id'] == 1
    
    def test_sell_matches_buy(self, book_with_callbacks):
        """Test sell matching against existing buy"""
        book, trades, events = book_with_callbacks
        
        # Add buy first
        lib.mx_order_book_add_limit(book, 1, SIDE_BUY, price_to_ticks(100.00), 50)
        trades.clear()
        
        # Add matching sell
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 50)
        
        assert trades.count() == 1
        assert trades.get_last()['aggressive_id'] == 2
        assert trades.get_last()['passive_id'] == 1

class TestPartialFills:
    """Test partial order fills"""
    
    def test_partial_fill_aggressive(self, book_with_callbacks):
        """Test aggressive order partially filled"""
        book, trades, events = book_with_callbacks
        
        # Add sell for 50
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 50)
        events.clear()
        
        # Add buy for 100 - should partially fill
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(100.00), 100)
        
        # Should have 1 trade for 50
        assert trades.count() == 1
        assert trades.get_last()['quantity'] == 50
        
        # Aggressive order should be partially filled
        order_events = events.get_for_order(2)
        assert any(e['event'] == EVENT_PARTIAL for e in order_events)
        
        # Remaining 50 should be in book
        qty_out = ffi.new("uint32_t*")
        lib.mx_order_book_get_order_info(book, 2, ffi.NULL, ffi.NULL, qty_out, ffi.NULL)
        assert qty_out[0] == 50  # Remaining quantity
    
    def test_partial_fill_passive(self, book_with_callbacks):
        """Test passive order partially filled"""
        book, trades, events = book_with_callbacks
        
        # Add sell for 100
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 100)
        events.clear()
        
        # Add buy for 50 - should partially fill passive
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(100.00), 50)
        
        # Trade for 50
        assert trades.count() == 1
        assert trades.get_last()['quantity'] == 50
        
        # Passive order (1) should still be in book with 50 remaining
        qty_out = ffi.new("uint32_t*")
        lib.mx_order_book_get_order_info(book, 1, ffi.NULL, ffi.NULL, qty_out, ffi.NULL)
        assert qty_out[0] == 50
    
    def test_multiple_partial_fills(self, book_with_callbacks):
        """Test order getting filled in multiple chunks"""
        book, trades, events = book_with_callbacks
        
        # Add large sell order
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 200)
        trades.clear()
        
        # Fill it in chunks
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(100.00), 50)
        lib.mx_order_book_add_limit(book, 3, SIDE_BUY, price_to_ticks(100.00), 50)
        lib.mx_order_book_add_limit(book, 4, SIDE_BUY, price_to_ticks(100.00), 50)
        lib.mx_order_book_add_limit(book, 5, SIDE_BUY, price_to_ticks(100.00), 50)
        
        # Should have 4 trades
        assert trades.count() == 4
        
        # Total volume should be 200
        assert trades.total_volume() == 200
        
        # Order 1 should be completely filled
        assert lib.mx_order_book_has_order(book, 1) == 0

class TestPriceTimePriority:
    """Test price-time priority matching"""
    
    def test_price_priority(self, book_with_callbacks):
        """Test that better prices match first"""
        book, trades, events = book_with_callbacks
        
        # Add two sell orders at different prices
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(101.00), 50)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 50)
        trades.clear()
        
        # Add buy that can match both - should match better price first
        lib.mx_order_book_add_limit(book, 3, SIDE_BUY, price_to_ticks(101.00), 50)
        
        # Should match with order 2 (lower price)
        assert trades.count() == 1
        assert trades.get_last()['passive_id'] == 2
        assert trades.get_last()['price'] == price_to_ticks(100.00)
    
    def test_time_priority_same_price(self, book_with_callbacks):
        """Test that earlier orders match first at same price"""
        book, trades, events = book_with_callbacks
        
        # Add three sell orders at same price
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 30)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 30)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(100.00), 30)
        trades.clear()
        
        # Add buy for 50 - should match orders in time order
        lib.mx_order_book_add_limit(book, 4, SIDE_BUY, price_to_ticks(100.00), 50)
        
        # Should have 2 trades
        assert trades.count() == 2
        
        all_trades = trades.get_all()
        # First trade with order 1 (30 shares)
        assert all_trades[0]['passive_id'] == 1
        assert all_trades[0]['quantity'] == 30
        
        # Second trade with order 2 (20 shares to complete 50)
        assert all_trades[1]['passive_id'] == 2
        assert all_trades[1]['quantity'] == 20
        
        # Order 1 should be gone (fully filled)
        assert lib.mx_order_book_has_order(book, 1) == 0
        
        # Order 2 should have 10 remaining
        qty_out = ffi.new("uint32_t*")
        lib.mx_order_book_get_order_info(book, 2, ffi.NULL, ffi.NULL, qty_out, ffi.NULL)
        assert qty_out[0] == 10
        
        # Order 3 should still have 30 (untouched)
        lib.mx_order_book_get_order_info(book, 3, ffi.NULL, ffi.NULL, qty_out, ffi.NULL)
        assert qty_out[0] == 30
    
    def test_time_priority_maintained_on_partial_fill(self, book_with_callbacks):
        """Test that partially filled orders maintain time priority"""
        book, trades, events = book_with_callbacks
        
        # Add three orders at same price
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 100)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(100.00), 100)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(100.00), 100)
        trades.clear()
        
        # Partially fill first order
        lib.mx_order_book_add_limit(book, 4, SIDE_BUY, price_to_ticks(100.00), 50)
        
        # Order 1 should have 50 remaining and still be first
        trades.clear()
        
        # Match again
        lib.mx_order_book_add_limit(book, 5, SIDE_BUY, price_to_ticks(100.00), 30)
        
        # Should match with order 1's remaining quantity
        assert trades.count() == 1
        assert trades.get_last()['passive_id'] == 1

class TestMarketOrders:
    """Test market order execution"""
    
    def test_market_buy_matches_best_ask(self, book_with_callbacks):
        """Test market buy matches at best ask"""
        book, trades, events = book_with_callbacks
        
        # Add sells at different prices
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 50)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(101.00), 50)
        trades.clear()
        
        # Market buy should match best ask
        lib.mx_order_book_add_market(book, 3, SIDE_BUY, 50)
        
        assert trades.count() == 1
        assert trades.get_last()['price'] == price_to_ticks(100.00)
        assert trades.get_last()['passive_id'] == 1
    
    def test_market_sell_matches_best_bid(self, book_with_callbacks):
        """Test market sell matches at best bid"""
        book, trades, events = book_with_callbacks
        
        # Add buys at different prices
        lib.mx_order_book_add_limit(book, 1, SIDE_BUY, price_to_ticks(100.00), 50)
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(99.00), 50)
        trades.clear()
        
        # Market sell should match best bid
        lib.mx_order_book_add_market(book, 3, SIDE_SELL, 50)
        
        assert trades.count() == 1
        assert trades.get_last()['price'] == price_to_ticks(100.00)
        assert trades.get_last()['passive_id'] == 1
    
    def test_market_order_walks_book(self, book_with_callbacks):
        """Test market order matching through multiple levels"""
        book, trades, events = book_with_callbacks
        
        # Add sells at multiple levels
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 30)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(101.00), 30)
        lib.mx_order_book_add_limit(book, 3, SIDE_SELL, price_to_ticks(102.00), 30)
        trades.clear()
        
        # Large market buy walks through levels
        lib.mx_order_book_add_market(book, 4, SIDE_BUY, 70)
        
        # Should have trades at multiple prices
        assert trades.count() == 3  # 30 + 30 + 10
        
        all_trades = trades.get_all()
        assert all_trades[0]['price'] == price_to_ticks(100.00)
        assert all_trades[0]['quantity'] == 30
        assert all_trades[1]['price'] == price_to_ticks(101.00)
        assert all_trades[1]['quantity'] == 30
        assert all_trades[2]['price'] == price_to_ticks(102.00)
        assert all_trades[2]['quantity'] == 10
    
    def test_market_order_no_liquidity(self, book_with_callbacks):
        """Test market order when there's no opposing liquidity"""
        book, trades, events = book_with_callbacks
        
        # Empty book - no asks
        result = lib.mx_order_book_add_market(book, 1, SIDE_BUY, 50)
        
        # Should succeed but not match anything
        assert result == STATUS_OK
        assert trades.count() == 0
        
        # Market order should not remain in book
        assert lib.mx_order_book_has_order(book, 1) == 0

class TestMultipleLevelMatching:
    """Test matching across multiple price levels"""
    
    def test_large_order_matches_multiple_levels(self, populated_book):
        """Test large order matching through multiple price levels"""
        book, trades, events = populated_book
        
        # Large buy order should match multiple ask levels
        # Populated book has: 200@100.50, 300@101.00, 100@101.50
        lib.mx_order_book_add_limit(book, 9999, SIDE_BUY, price_to_ticks(102.00), 500)
        
        # Should match all three levels (200 + 300 = 500)
        assert trades.count() >= 2
        
        # Total matched should be 500
        assert trades.total_volume() == 500
    
    def test_order_removal_after_full_fill(self, book_with_callbacks):
        """Test that fully filled orders are removed from book"""
        book, trades, events = book_with_callbacks
        
        # Add order
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 50)
        trades.clear()
        
        # Match it completely
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(100.00), 50)
        
        # Both orders should be removed
        assert lib.mx_order_book_has_order(book, 1) == 0
        assert lib.mx_order_book_has_order(book, 2) == 0
    
    def test_best_price_updates_after_match(self, book_with_callbacks):
        """Test that best prices update correctly after matching"""
        book, trades, events = book_with_callbacks
        
        # Add two ask levels
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 50)
        lib.mx_order_book_add_limit(book, 2, SIDE_SELL, price_to_ticks(101.00), 50)
        
        # Best ask should be 100
        assert lib.mx_order_book_get_best_ask(book) == price_to_ticks(100.00)
        
        # Match first level
        lib.mx_order_book_add_limit(book, 3, SIDE_BUY, price_to_ticks(100.00), 50)
        
        # Best ask should now be 101
        assert lib.mx_order_book_get_best_ask(book) == price_to_ticks(101.00)

class TestOrderEvents:
    """Test order event callbacks"""
    
    def test_order_accepted_event(self, book_with_callbacks):
        """Test that accepted orders trigger event"""
        book, trades, events = book_with_callbacks
        
        # Add order that won't match
        lib.mx_order_book_add_limit(book, 1, SIDE_BUY, price_to_ticks(99.00), 50)
        
        # Should have acceptance event
        order_events = events.get_for_order(1)
        assert len(order_events) > 0
        assert any(e['event'] == EVENT_ACCEPTED for e in order_events)
    
    def test_order_filled_event(self, book_with_callbacks):
        """Test that filled orders trigger event"""
        book, trades, events = book_with_callbacks
        
        # Add passive order
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 50)
        events.clear()
        
        # Match it completely
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(100.00), 50)
        
        # Both should have filled events
        order1_events = events.get_for_order(1)
        order2_events = events.get_for_order(2)
        
        assert any(e['event'] == EVENT_FILLED for e in order1_events)
        assert any(e['event'] == EVENT_FILLED for e in order2_events)
    
    def test_order_partial_event(self, book_with_callbacks):
        """Test partial fill event"""
        book, trades, events = book_with_callbacks
        
        # Add order
        lib.mx_order_book_add_limit(book, 1, SIDE_SELL, price_to_ticks(100.00), 100)
        events.clear()
        
        # Partially fill it
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(100.00), 50)
        
        # Should have partial fill event for order 1
        order1_events = events.get_for_order(1)
        partial_events = [e for e in order1_events if e['event'] == EVENT_PARTIAL]
        
        assert len(partial_events) > 0
        assert partial_events[0]['filled_qty'] == 50
        assert partial_events[0]['remaining_qty'] == 50

class TestEdgeCases:
    """Test edge cases in matching"""
    
    def test_zero_quantity_rejected(self, order_book):
        """Test that zero quantity orders are rejected"""
        from testhelpers import STATUS_INVALID_QUANTITY
        
        result = lib.mx_order_book_add_limit(
            order_book, 1, SIDE_BUY, price_to_ticks(100.00), 0
        )
        assert result == STATUS_INVALID_QUANTITY
    
    def test_zero_price_rejected(self, order_book):
        """Test that zero price limit orders are rejected"""
        from testhelpers import STATUS_INVALID_PRICE
        
        result = lib.mx_order_book_add_limit(
            order_book, 1, SIDE_BUY, 0, 50
        )
        assert result == STATUS_INVALID_PRICE
    
    def test_matching_own_side(self, book_with_callbacks):
        """Test that orders don't match with same side"""
        book, trades, events = book_with_callbacks
        
        # Add two buy orders at same price
        lib.mx_order_book_add_limit(book, 1, SIDE_BUY, price_to_ticks(100.00), 50)
        lib.mx_order_book_add_limit(book, 2, SIDE_BUY, price_to_ticks(100.00), 50)
        
        # Should be no trades
        assert trades.count() == 0
        
        # Both should be in book
        assert lib.mx_order_book_has_order(book, 1) == 1
        assert lib.mx_order_book_has_order(book, 2) == 1
