"""
Test helpers - CFFI setup for loading the MatchX library
Following the article's pattern for beautiful native library testing
"""

import os
import sys
import subprocess
from cffi import FFI

# Determine the library path based on platform
def get_library_path():
    """Get the path to the compiled library"""
    here = os.path.abspath(os.path.dirname(__file__))
    project_root = os.path.dirname(here)
    
    if sys.platform == 'darwin':
        # macOS
        lib_name = 'libmatchengine.dylib'
    elif sys.platform == 'win32':
        # Windows
        lib_name = 'matchengine.dll'
    else:
        # Linux
        lib_name = 'libmatchengine.so'
    
    # Try multiple possible build locations
    possible_paths = [
        os.path.join(project_root, 'build', lib_name),
        os.path.join(project_root, 'bin', 'Debug', lib_name),
        os.path.join(project_root, 'bin', 'Release', lib_name),
    ]
    
    for path in possible_paths:
        if os.path.exists(path):
            return path
    
    raise RuntimeError(f"Could not find library {lib_name} in any of: {possible_paths}")

# Initialize CFFI
ffi = FFI()

# Get header path
here = os.path.abspath(os.path.dirname(__file__))
project_root = os.path.dirname(here)
header_path = os.path.join(project_root, 'include', 'matchengine.h')

# Preprocess the header with C preprocessor
# This removes includes and processes macros
try:
    result = subprocess.run(
        ['cc', '-E', '-DMX_API=', '-P', header_path],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=True
    )
    header_content = result.stdout
except subprocess.CalledProcessError as e:
    print(f"Error preprocessing header: {e.stderr}")
    raise
except FileNotFoundError:
    # Fallback: read header directly and strip includes
    with open(header_path, 'r') as f:
        lines = []
        for line in f:
            # Skip includes and preprocessor directives
            if line.strip().startswith('#'):
                continue
            # Replace MX_API with nothing
            line = line.replace('MX_API', '')
            lines.append(line)
        header_content = ''.join(lines)

# Parse the header
ffi.cdef(header_content)

# Load the library
lib_path = get_library_path()
lib = ffi.dlopen(lib_path)

print(f"Loaded MatchX library from: {lib_path}")
print(f"Library version: {lib.mx_get_version()}")

# Helper functions for testing

def create_context():
    """Create a new context"""
    return lib.mx_context_new()

def free_context(ctx):
    """Free a context"""
    if ctx:
        lib.mx_context_free(ctx)

def create_order_book(ctx, symbol):
    """Create a new order book"""
    symbol_bytes = symbol.encode('utf-8') if isinstance(symbol, str) else symbol
    return lib.mx_order_book_new(ctx, symbol_bytes)

def free_order_book(book):
    """Free an order book"""
    if book:
        lib.mx_order_book_free(book)

# Price conversion helpers (for readability in tests)
def price_to_ticks(price_float):
    """Convert float price to integer ticks (e.g., $100.50 -> 10050)"""
    return int(price_float * 100)

def ticks_to_price(ticks):
    """Convert integer ticks to float price (e.g., 10050 -> $100.50)"""
    return ticks / 100.0

# Callback storage for tests
_callback_storage = {}

def create_trade_callback(func):
    """Create a C callback for trades"""
    @ffi.callback("void(void*, uint64_t, uint64_t, uint32_t, uint32_t, uint64_t)")
    def callback(user_data, aggressive_id, passive_id, price, quantity, timestamp):
        func(aggressive_id, passive_id, price, quantity, timestamp)
    
    # Store to prevent garbage collection
    _callback_storage[id(func)] = callback
    return callback

def create_order_callback(func):
    """Create a C callback for order events"""
    @ffi.callback("void(void*, uint64_t, int, uint32_t, uint32_t)")
    def callback(user_data, order_id, event, filled_qty, remaining_qty):
        func(order_id, event, filled_qty, remaining_qty)
    
    # Store to prevent garbage collection
    _callback_storage[id(func)] = callback
    return callback

# Constants for easy access
SIDE_BUY = lib.MX_SIDE_BUY
SIDE_SELL = lib.MX_SIDE_SELL

ORDER_TYPE_LIMIT = lib.MX_ORDER_TYPE_LIMIT
ORDER_TYPE_MARKET = lib.MX_ORDER_TYPE_MARKET
ORDER_TYPE_STOP = lib.MX_ORDER_TYPE_STOP
ORDER_TYPE_STOP_LIMIT = lib.MX_ORDER_TYPE_STOP_LIMIT

TIF_GTC = lib.MX_TIF_GTC
TIF_IOC = lib.MX_TIF_IOC
TIF_FOK = lib.MX_TIF_FOK
TIF_DAY = lib.MX_TIF_DAY
TIF_GTD = lib.MX_TIF_GTD

FLAG_NONE = lib.MX_ORDER_FLAG_NONE
FLAG_POST_ONLY = lib.MX_ORDER_FLAG_POST_ONLY
FLAG_HIDDEN = lib.MX_ORDER_FLAG_HIDDEN
FLAG_AON = lib.MX_ORDER_FLAG_AON

STATUS_OK = lib.MX_STATUS_OK
STATUS_ORDER_NOT_FOUND = lib.MX_STATUS_ORDER_NOT_FOUND
STATUS_DUPLICATE_ORDER = lib.MX_STATUS_DUPLICATE_ORDER
STATUS_WOULD_MATCH = lib.MX_STATUS_WOULD_MATCH
STATUS_CANNOT_FILL = lib.MX_STATUS_CANNOT_FILL

EVENT_ACCEPTED = lib.MX_EVENT_ORDER_ACCEPTED
EVENT_REJECTED = lib.MX_EVENT_ORDER_REJECTED
EVENT_FILLED = lib.MX_EVENT_ORDER_FILLED
EVENT_PARTIAL = lib.MX_EVENT_ORDER_PARTIAL
EVENT_CANCELLED = lib.MX_EVENT_ORDER_CANCELLED
EVENT_EXPIRED = lib.MX_EVENT_ORDER_EXPIRED
EVENT_TRIGGERED = lib.MX_EVENT_ORDER_TRIGGERED
