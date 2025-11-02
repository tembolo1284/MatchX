"""
Basic functionality tests
Tests context creation, order books, and simple order operations
"""

import pytest
from testhelpers import (
    ffi, lib,
    SIDE_BUY, SIDE_SELL,
    STATUS_OK, STATUS_ORDER_NOT_FOUND, STATUS_DUPLICATE_ORDER,
    price_to_ticks, ticks_to_price
)

class TestVersionAndCompatibility:
    """Test version information"""
    
    def test_get_version(self):
        """Test that we can get library version"""
        version = lib.mx_get_version()
        assert version > 0
        
        major = (version >> 16) & 0xFF
        minor = (version >> 8) & 0xFF
        patch = version & 0xFF
        
        # Should be version 1.0.0
        assert major == 1
        assert minor == 0
        assert patch == 0
    
    def test_compatibility(self):
        """Test DLL compatibility check"""
        is_compatible = lib.mx_is_compatible_dll()
        assert is_compatible == 1

class TestContext:
    """Test context management"""
    
    def test_create_and_free_context(self):
        """Test basic context lifecycle"""
        ctx = lib.mx_context_new()
        assert ctx != ffi.NULL
        lib.mx_context_free(ctx)
    
    def test_set_timestamp(self, context):
        """Test setting and getting timestamp"""
        timestamp = 1234567890000000000  # nanoseconds
        lib.mx_context_set_timestamp(context, timestamp)
        
        retrieved = lib.mx_context_get_timestamp(context)
        assert retrieved == timestamp
    
    def test_callbacks_without_crash(self, context):
        """Test that setting NULL callbacks doesn't crash"""
        lib.mx_context_set_callbacks(context, ffi.NULL, ffi.NULL, ffi.NULL)

class TestOrderBook:
    """Test order book management"""
    
    def test_create_and_free_order_book(self, context):
        """Test basic order book lifecycle"""
        book = lib.mx_order_book_new(context, b"AAPL")
        assert book != ffi.NULL
        lib.mx_order_book_free(book)
    
    def test_get_symbol(self, order_book):
        """Test retrieving order book symbol"""
        symbol = lib.mx_order_book_get_symbol(order_book)
        assert ffi.string(symbol) == b"TEST"
    
    def test_initial_market_state(self, order_book):
        """Test that new order book has no market"""
        best_bid = lib.mx_order_book_get_best_bid(order_book)
        best_ask = lib.mx_order_book_get_best_ask(order_book)
        spread = lib.mx_order_book_get_spread(order_book)
        mid = lib.mx_order_book_get_mid_price(order_book)
        
        assert best_bid == 0
        assert best_ask == 0
        assert spread == 0
        assert mid == 0
    
    def test_clear_empty_book(self, order_book):
        """Test clearing an empty book doesn't crash"""
        lib.mx_order_book_clear(order_book)

class TestLimitOrders:
    """Test limit order operations"""
    
    def test_add_single_bid(self, order_book):
        """Test adding a single buy limit order"""
        result = lib.mx_order_book_add_limit(
            order_book, 
            1,  # order_id
            SIDE_BUY, 
            price_to_ticks(100.00), 
            50  # quantity
        )
        
        assert result == STATUS_OK
        
        # Check best bid is set
        best_bid = lib.mx_order_book_get_best_bid(order_book)
        assert best_bid == price_to_ticks(100.00)
    
    def test_add_single_ask(self, order_book):
        """Test adding a single sell limit order"""
        result = lib.mx_order_book_add_limit(
            order_book,
            1,
            SIDE_SELL,
            price_to_ticks(101.00),
            50
        )
        
        assert result == STATUS_OK
        
        # Check best ask is set
        best_ask = lib.mx_order_book_get_best_ask(order_book)
        assert best_ask == price_to_ticks(101.00)
    
    def test_add_multiple_bids(self, order_book):
        """Test adding multiple buy orders at different prices"""
        prices = [99.50, 100.00, 98.50]
        
        for i, price in enumerate(prices):
            result = lib.mx_order_book_add_limit(
                order_book,
                i + 1,
                SIDE_BUY,
                price_to_ticks(price),
                100
            )
            assert result == STATUS_OK
        
        # Best bid should be highest price
        best_bid = lib.mx_order_book_get_best_bid(order_book)
        assert best_bid == price_to_ticks(100.00)
    
    def test_add_multiple_asks(self, order_book):
        """Test adding multiple sell orders at different prices"""
        prices = [101.00, 100.50, 101.50]
        
        for i, price in enumerate(prices):
            result = lib.mx_order_book_add_limit(
                order_book,
                i + 1,
                SIDE_SELL,
                price_to_ticks(price),
                100
            )
            assert result == STATUS_OK
        
        # Best ask should be lowest price
        best_ask = lib.mx_order_book_get_best_ask(order_book)
        assert best_ask == price_to_ticks(100.50)
    
    def test_duplicate_order_id(self, order_book):
        """Test that duplicate order IDs are rejected"""
        # Add first order
        result = lib.mx_order_book_add_limit(
            order_book, 1, SIDE_BUY, price_to_ticks(100.00), 50
        )
        assert result == STATUS_OK
        
        # Try to add with same ID
        result = lib.mx_order_book_add_limit(
            order_book, 1, SIDE_SELL, price_to_ticks(101.00), 50
        )
        assert result == STATUS_DUPLICATE_ORDER
    
    def test_spread_calculation(self, order_book):
        """Test spread calculation with bid and ask"""
        # Add bid at $100
        lib.mx_order_book_add_limit(
            order_book, 1, SIDE_BUY, price_to_ticks(100.00), 50
        )
        
        # Add ask at $101
        lib.mx_order_book_add_limit(
            order_book, 2, SIDE_SELL, price_to_ticks(101.00), 50
        )
        
        spread = lib.mx_order_book_get_spread(order_book)
        assert spread == price_to_ticks(1.00)  # $1.00 spread
    
    def test_mid_price(self, order_book):
        """Test mid price calculation"""
        # Add bid at $100
        lib.mx_order_book_add_limit(
            order_book, 1, SIDE_BUY, price_to_ticks(100.00), 50
        )
        
        # Add ask at $102
        lib.mx_order_book_add_limit(
            order_book, 2, SIDE_SELL, price_to_ticks(102.00), 50
        )
        
        mid = lib.mx_order_book_get_mid_price(order_book)
        assert mid == price_to_ticks(101.00)  # Average of $100 and $102

class TestOrderCancellation:
    """Test order cancellation"""
    
    def test_cancel_existing_order(self, order_book):
        """Test cancelling an order that exists"""
        # Add order
        lib.mx_order_book_add_limit(
            order_book, 1, SIDE_BUY, price_to_ticks(100.00), 50
        )
        
        # Cancel it
        result = lib.mx_order_book_cancel(order_book, 1)
        assert result == STATUS_OK
        
        # Best bid should be 0 now
        best_bid = lib.mx_order_book_get_best_bid(order_book)
        assert best_bid == 0
    
    def test_cancel_nonexistent_order(self, order_book):
        """Test cancelling an order that doesn't exist"""
        result = lib.mx_order_book_cancel(order_book, 999)
        assert result == STATUS_ORDER_NOT_FOUND
    
    def test_cancel_updates_best_prices(self, order_book):
        """Test that cancelling best bid/ask updates prices correctly"""
        # Add two bids
        lib.mx_order_book_add_limit(
            order_book, 1, SIDE_BUY, price_to_ticks(100.00), 50
        )
        lib.mx_order_book_add_limit(
            order_book, 2, SIDE_BUY, price_to_ticks(99.00), 50
        )
        
        # Best bid should be $100
        assert lib.mx_order_book_get_best_bid(order_book) == price_to_ticks(100.00)
        
        # Cancel best bid
        lib.mx_order_book_cancel(order_book, 1)
        
        # Best bid should now be $99
        assert lib.mx_order_book_get_best_bid(order_book) == price_to_ticks(99.00)

class TestOrderQueries:
    """Test order query functions"""
    
    def test_has_order(self, order_book):
        """Test checking if order exists"""
        # Should not exist initially
        assert lib.mx_order_book_has_order(order_book, 1) == 0
        
        # Add order
        lib.mx_order_book_add_limit(
            order_book, 1, SIDE_BUY, price_to_ticks(100.00), 50
        )
        
        # Should exist now
        assert lib.mx_order_book_has_order(order_book, 1) == 1
        
        # Cancel it
        lib.mx_order_book_cancel(order_book, 1)
        
        # Should not exist anymore
        assert lib.mx_order_book_has_order(order_book, 1) == 0
    
    def test_get_order_info(self, order_book):
        """Test retrieving order information"""
        # Add order
        order_id = 1
        price = price_to_ticks(100.50)
        quantity = 75
        
        lib.mx_order_book_add_limit(
            order_book, order_id, SIDE_BUY, price, quantity
        )
        
        # Query order info
        side_out = ffi.new("int*")
        price_out = ffi.new("uint32_t*")
        quantity_out = ffi.new("uint32_t*")
        filled_out = ffi.new("uint32_t*")
        
        result = lib.mx_order_book_get_order_info(
            order_book, order_id,
            side_out, price_out, quantity_out, filled_out
        )
        
        assert result == STATUS_OK
        assert side_out[0] == SIDE_BUY
        assert price_out[0] == price
        assert quantity_out[0] == quantity
        assert filled_out[0] == 0  # Not filled yet

class TestOrderBookStats:
    """Test order book statistics"""
    
    def test_empty_book_stats(self, order_book):
        """Test stats on empty book"""
        total_orders = ffi.new("uint32_t*")
        bid_levels = ffi.new("uint32_t*")
        ask_levels = ffi.new("uint32_t*")
        
        lib.mx_order_book_get_stats(
            order_book, total_orders, bid_levels, ask_levels, ffi.NULL, ffi.NULL
        )
        
        assert total_orders[0] == 0
        assert bid_levels[0] == 0
        assert ask_levels[0] == 0
    
    def test_populated_book_stats(self, order_book):
        """Test stats on populated book"""
        # Add 3 bid levels
        lib.mx_order_book_add_limit(order_book, 1, SIDE_BUY, price_to_ticks(100.00), 50)
        lib.mx_order_book_add_limit(order_book, 2, SIDE_BUY, price_to_ticks(99.00), 50)
        lib.mx_order_book_add_limit(order_book, 3, SIDE_BUY, price_to_ticks(98.00), 50)
        
        # Add 2 ask levels
        lib.mx_order_book_add_limit(order_book, 4, SIDE_SELL, price_to_ticks(101.00), 50)
        lib.mx_order_book_add_limit(order_book, 5, SIDE_SELL, price_to_ticks(102.00), 50)
        
        total_orders = ffi.new("uint32_t*")
        bid_levels = ffi.new("uint32_t*")
        ask_levels = ffi.new("uint32_t*")
        
        lib.mx_order_book_get_stats(
            order_book, total_orders, bid_levels, ask_levels, ffi.NULL, ffi.NULL
        )
        
        assert total_orders[0] == 5
        assert bid_levels[0] == 3
        assert ask_levels[0] == 2
    
    def test_volume_at_price(self, order_book):
        """Test getting volume at specific price"""
        price = price_to_ticks(100.00)
        
        # Initially 0
        volume = lib.mx_order_book_get_volume_at_price(order_book, SIDE_BUY, price)
        assert volume == 0
        
        # Add orders at same price
        lib.mx_order_book_add_limit(order_book, 1, SIDE_BUY, price, 50)
        lib.mx_order_book_add_limit(order_book, 2, SIDE_BUY, price, 30)
        lib.mx_order_book_add_limit(order_book, 3, SIDE_BUY, price, 20)
        
        # Total volume should be sum
        volume = lib.mx_order_book_get_volume_at_price(order_book, SIDE_BUY, price)
        assert volume == 100

class TestClearBook:
    """Test clearing the order book"""
    
    def test_clear_removes_all_orders(self, order_book):
        """Test that clear removes all orders"""
        # Add several orders
        lib.mx_order_book_add_limit(order_book, 1, SIDE_BUY, price_to_ticks(100.00), 50)
        lib.mx_order_book_add_limit(order_book, 2, SIDE_BUY, price_to_ticks(99.00), 50)
        lib.mx_order_book_add_limit(order_book, 3, SIDE_SELL, price_to_ticks(101.00), 50)
        
        # Verify they exist
        total_orders = ffi.new("uint32_t*")
        lib.mx_order_book_get_stats(order_book, total_orders, ffi.NULL, ffi.NULL, ffi.NULL, ffi.NULL)
        assert total_orders[0] == 3
        
        # Clear
        lib.mx_order_book_clear(order_book)
        
        # Verify empty
        lib.mx_order_book_get_stats(order_book, total_orders, ffi.NULL, ffi.NULL, ffi.NULL, ffi.NULL)
        assert total_orders[0] == 0
        assert lib.mx_order_book_get_best_bid(order_book) == 0
        assert lib.mx_order_book_get_best_ask(order_book) == 0

class TestUtilityFunctions:
    """Test utility functions"""
    
    def test_status_messages(self):
        """Test getting human-readable status messages"""
        msg = lib.mx_status_message(STATUS_OK)
        assert ffi.string(msg) == b"Success"
        
        msg = lib.mx_status_message(STATUS_ORDER_NOT_FOUND)
        assert b"not found" in ffi.string(msg).lower()
    
    def test_order_type_names(self):
        """Test getting order type names"""
        from testhelpers import ORDER_TYPE_LIMIT, ORDER_TYPE_MARKET
        
        name = lib.mx_order_type_name(ORDER_TYPE_LIMIT)
        assert ffi.string(name) == b"LIMIT"
        
        name = lib.mx_order_type_name(ORDER_TYPE_MARKET)
        assert ffi.string(name) == b"MARKET"
    
    def test_tif_names(self):
        """Test getting time-in-force names"""
        from testhelpers import TIF_GTC, TIF_IOC, TIF_FOK
        
        name = lib.mx_tif_name(TIF_GTC)
        assert ffi.string(name) == b"GTC"
        
        name = lib.mx_tif_name(TIF_IOC)
        assert ffi.string(name) == b"IOC"
        
        name = lib.mx_tif_name(TIF_FOK)
        assert ffi.string(name) == b"FOK"
