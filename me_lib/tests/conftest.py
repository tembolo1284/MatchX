"""
pytest configuration and fixtures
Provides common setup for all tests
"""

import pytest
from testhelpers import (
    ffi, lib,
    create_context, free_context,
    create_order_book, free_order_book
)

@pytest.fixture(scope='function')
def context():
    """
    Create a fresh context for each test
    Automatically cleaned up after test completes
    """
    ctx = create_context()
    assert ctx != ffi.NULL, "Failed to create context"
    
    yield ctx
    
    # Cleanup
    free_context(ctx)

@pytest.fixture(scope='function')
def order_book(context):
    """
    Create a fresh order book for each test
    Automatically cleaned up after test completes
    """
    book = create_order_book(context, "TEST")
    assert book != ffi.NULL, "Failed to create order book"
    
    yield book
    
    # Cleanup
    free_order_book(book)

@pytest.fixture(scope='function')
def btc_book(context):
    """
    Create an order book for BTC/USD
    """
    book = create_order_book(context, "BTCUSD")
    assert book != ffi.NULL, "Failed to create BTC order book"
    
    yield book
    
    free_order_book(book)

@pytest.fixture(scope='function')
def trade_recorder():
    """
    Fixture that records trades for verification
    Returns a TradeRecorder object
    """
    class TradeRecorder:
        def __init__(self):
            self.trades = []
        
        def record(self, aggressive_id, passive_id, price, quantity, timestamp):
            self.trades.append({
                'aggressive_id': aggressive_id,
                'passive_id': passive_id,
                'price': price,
                'quantity': quantity,
                'timestamp': timestamp
            })
        
        def clear(self):
            self.trades.clear()
        
        def count(self):
            return len(self.trades)
        
        def get_last(self):
            return self.trades[-1] if self.trades else None
        
        def get_all(self):
            return self.trades.copy()
        
        def total_volume(self):
            return sum(t['quantity'] for t in self.trades)
    
    return TradeRecorder()

@pytest.fixture(scope='function')
def order_event_recorder():
    """
    Fixture that records order events for verification
    Returns an OrderEventRecorder object
    """
    class OrderEventRecorder:
        def __init__(self):
            self.events = []
        
        def record(self, order_id, event, filled_qty, remaining_qty):
            self.events.append({
                'order_id': order_id,
                'event': event,
                'filled_qty': filled_qty,
                'remaining_qty': remaining_qty
            })
        
        def clear(self):
            self.events.clear()
        
        def count(self):
            return len(self.events)
        
        def get_last(self):
            return self.events[-1] if self.events else None
        
        def get_all(self):
            return self.events.copy()
        
        def get_for_order(self, order_id):
            return [e for e in self.events if e['order_id'] == order_id]
    
    return OrderEventRecorder()

@pytest.fixture(scope='function')
def book_with_callbacks(context, trade_recorder, order_event_recorder):
    """
    Create an order book with callbacks already set up
    Returns (book, trade_recorder, order_event_recorder)
    """
    from testhelpers import create_trade_callback, create_order_callback
    
    # Create callbacks
    trade_cb = create_trade_callback(trade_recorder.record)
    order_cb = create_order_callback(order_event_recorder.record)
    
    # Set callbacks on context
    lib.mx_context_set_callbacks(context, trade_cb, order_cb, ffi.NULL)
    
    # Create order book
    book = create_order_book(context, "TEST")
    assert book != ffi.NULL, "Failed to create order book"
    
    yield (book, trade_recorder, order_event_recorder)
    
    # Cleanup
    free_order_book(book)

@pytest.fixture(scope='function')
def populated_book(book_with_callbacks):
    """
    Create a populated order book with some orders already in it
    Returns (book, trade_recorder, order_event_recorder)
    
    Order book setup:
    Bids:                   Asks:
    100 @ $99.50           200 @ $100.50
    150 @ $99.00           300 @ $101.00
    200 @ $98.50           100 @ $101.50
    """
    from testhelpers import SIDE_BUY, SIDE_SELL, price_to_ticks
    
    book, trades, events = book_with_callbacks
    
    # Clear any initialization events
    trades.clear()
    events.clear()
    
    # Add bid orders (buy side)
    lib.mx_order_book_add_limit(book, 1001, SIDE_BUY, price_to_ticks(99.50), 100)
    lib.mx_order_book_add_limit(book, 1002, SIDE_BUY, price_to_ticks(99.00), 150)
    lib.mx_order_book_add_limit(book, 1003, SIDE_BUY, price_to_ticks(98.50), 200)
    
    # Add ask orders (sell side)
    lib.mx_order_book_add_limit(book, 2001, SIDE_SELL, price_to_ticks(100.50), 200)
    lib.mx_order_book_add_limit(book, 2002, SIDE_SELL, price_to_ticks(101.00), 300)
    lib.mx_order_book_add_limit(book, 2003, SIDE_SELL, price_to_ticks(101.50), 100)
    
    # Clear events from setup
    trades.clear()
    events.clear()
    
    yield (book, trades, events)

@pytest.fixture(scope='session')
def check_version():
    """
    Session-scoped fixture to check library version compatibility
    Runs once at the start of the test session
    """
    version = lib.mx_get_version()
    major = (version >> 16) & 0xFF
    minor = (version >> 8) & 0xFF
    patch = version & 0xFF
    
    print(f"\nMatchX Library Version: {major}.{minor}.{patch}")
    
    # Check compatibility
    is_compatible = lib.mx_is_compatible_dll()
    assert is_compatible, "Library version is not compatible with header"
    
    return version

@pytest.fixture(autouse=True, scope='session')
def setup_session(check_version):
    """
    Auto-use fixture that runs once per session
    Prints library information
    """
    print("\n" + "="*60)
    print("MatchX Matching Engine Test Suite")
    print("="*60)

# Markers for organizing tests
def pytest_configure(config):
    """Configure custom pytest markers"""
    config.addinivalue_line(
        "markers", "slow: marks tests as slow (deselect with '-m \"not slow\"')"
    )
    config.addinivalue_line(
        "markers", "performance: marks performance/benchmark tests"
    )
    config.addinivalue_line(
        "markers", "integration: marks integration tests"
    )
    config.addinivalue_line(
        "markers", "stress: marks stress tests with many orders"
    )
