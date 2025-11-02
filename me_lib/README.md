# MatchX - High Performance Order Matching Engine

A professional-grade order matching engine library written in C++ with a clean C API. Designed for high-frequency trading applications with microsecond-level latency and industry-standard price-time priority.

## Features

### Core Capabilities
- **Price-Time Priority Matching** - Industry-standard FIFO matching at each price level
- **Zero-Allocation Hot Path** - Memory pools and intrusive lists for predictable performance
- **O(1) Order Operations** - Cancel, modify, and lookup in constant time
- **Multiple Order Types** - Limit, Market, Stop, Stop-Limit
- **Advanced Time-in-Force** - GTC, IOC, FOK, DAY, GTD
- **Special Order Flags** - Post-Only, Iceberg/Hidden, All-or-None, Reduce-Only
- **Partial Fills** - Full support with time priority preservation
- **Custom Memory Allocators** - User-controllable memory allocation

### Design Philosophy
Built following [Armin Ronacher's principles for beautiful native libraries](https://lucumr.pocoo.org/2013/8/18/beautiful-native-libraries/):
- Single public header (`matchengine.h`)
- Pure C API with opaque types
- No global state - everything through context objects
- Custom allocators for embedded/specialized environments
- ABI stability through version checking
- Clean separation of C interface and C++ implementation

## Architecture
```
┌─────────────────────────────────────────────────┐
│           Public C API (matchengine.h)          │
│  Context, OrderBook, Orders, Callbacks, Queries │
└─────────────────────────────────────────────────┘
                      │
                      ▼
┌─────────────────────────────────────────────────┐
│         C++ Implementation (Internal)           │
├─────────────────────────────────────────────────┤
│  OrderBook                                      │
│    ├─ PriceLevel (Intrusive Lists)             │
│    │    └─ Order (FIFO Queue)                  │
│    ├─ OrderPool (Memory Pool)                  │
│    └─ HashMap (O(1) Order Lookup)              │
│                                                 │
│  Custom Allocators + Zero-Copy Design          │
└─────────────────────────────────────────────────┘
```

### Key Data Structures

1. **Price Levels** - `std::map` for price-sorted access (O(log n) insert)
   - For bounded prices: Can use array indexing for O(1) access
   
2. **Order Queues** - Intrusive doubly-linked lists per price level
   - O(1) insert at tail (time priority)
   - O(1) remove from anywhere (cancellation)
   - O(1) access to front (matching)

3. **Order Lookup** - Hash map for O(1) order ID → Order*
   - Fast cancellation and queries
   - Custom hash function for uint64_t IDs

4. **Memory Pool** - Pre-allocated Order objects
   - No malloc/free in hot path
   - Freelist for reuse

## Building

### Requirements
- C++14 compiler (GCC 7+, Clang 5+, MSVC 2017+)
- Premake5 (for build file generation)
- Python 3.7+ with CFFI (for tests)

### Quick Start
```bash
# Generate build files
premake5 gmake2        # Linux/macOS Makefiles
premake5 vs2022        # Visual Studio 2022
premake5 xcode4        # Xcode

# Build
make config=release    # Linux/macOS
# Or open .sln in Visual Studio / .xcworkspace in Xcode

# The library will be in bin/Release-<platform>-x86_64/
```

### Build Configurations

- **Debug** - Symbols, no optimization, assertions enabled
- **Release** - Full optimization (-O3), LTO, no debug symbols

### Platform Notes

**Linux:**
```bash
premake5 gmake2
make config=release
# Output: bin/Release-linux-x86_64/libmatchengine.so
```

**macOS:**
```bash
premake5 gmake2
make config=release
# Output: bin/Release-macosx-x86_64/libmatchengine.dylib
```

**Windows:**
```bash
premake5 vs2022
# Open MatchEngine.sln in Visual Studio
# Build in Release mode
# Output: bin\Release-windows-x86_64\matchengine.dll
```

## Usage

### Basic Example (C)
```c
#include "matchengine.h"
#include <stdio.h>

void on_trade(void* user_data, uint64_t buy_id, uint64_t sell_id,
              uint32_t price, uint32_t quantity, uint64_t timestamp) {
    printf("TRADE: Buy #%llu × Sell #%llu @ %u for %u\n",
           buy_id, sell_id, price, quantity);
}

int main() {
    // Create context
    mx_context_t* ctx = mx_context_new();
    
    // Set callbacks
    mx_context_set_callbacks(ctx, on_trade, NULL, NULL);
    
    // Create order book
    mx_order_book_t* book = mx_order_book_new(ctx, "AAPL");
    
    // Add limit orders (prices in ticks, e.g., $100.00 = 10000)
    mx_order_book_add_limit(book, 1, MX_SIDE_SELL, 10000, 100);  // Sell 100 @ $100.00
    mx_order_book_add_limit(book, 2, MX_SIDE_BUY,  10000, 50);   // Buy 50 @ $100.00 → MATCH!
    
    // Query market data
    uint32_t best_bid = mx_order_book_get_best_bid(book);
    uint32_t best_ask = mx_order_book_get_best_ask(book);
    printf("Best Bid: %u, Best Ask: %u\n", best_bid, best_ask);
    
    // Cleanup
    mx_order_book_free(book);
    mx_context_free(ctx);
    
    return 0;
}
```

### Advanced Features
```c
// Iceberg order (only show 50, but total 500)
mx_order_book_add_order(book, 100, MX_ORDER_TYPE_LIMIT, MX_SIDE_BUY,
                       10000, 0, 500, 50, MX_TIF_GTC, MX_ORDER_FLAG_HIDDEN, 0);

// Fill-or-Kill order
mx_order_book_add_order(book, 101, MX_ORDER_TYPE_LIMIT, MX_SIDE_SELL,
                       10000, 0, 200, 0, MX_TIF_FOK, MX_ORDER_FLAG_NONE, 0);

// Post-Only order (reject if would match immediately)
mx_order_book_add_order(book, 102, MX_ORDER_TYPE_LIMIT, MX_SIDE_BUY,
                       10000, 0, 100, 0, MX_TIF_GTC, MX_ORDER_FLAG_POST_ONLY, 0);

// Stop-limit order
mx_order_book_add_order(book, 103, MX_ORDER_TYPE_STOP_LIMIT, MX_SIDE_SELL,
                       9900, 10000, 100, 0, MX_TIF_GTC, MX_ORDER_FLAG_NONE, 0);
```

### Custom Allocators
```c
// Set custom allocators before creating any objects
mx_set_allocators(my_malloc, my_realloc, my_free);

// Now all allocations use your functions
mx_context_t* ctx = mx_context_new();
// ...
```

## Testing

The library uses Python + CFFI for fast, flexible testing without compilation overhead.

### Running Tests
```bash
cd tests/

# Run all tests
pytest -v

# Run specific test categories
pytest -v test_basic.py
pytest -v test_matching.py
pytest -v test_price_time_priority.py

# Run performance tests
pytest -v -m performance test_performance.py

# Run stress tests
pytest -v -m stress test_performance.py
```

### Test Categories

- `test_basic.py` - Context, order books, basic operations
- `test_matching.py` - Order matching, partial fills, price-time priority
- `test_price_time_priority.py` - Rigorous priority testing
- `test_performance.py` - Throughput, latency, stress tests

## Performance Characteristics

### Theoretical Complexity

| Operation | Complexity | Notes |
|-----------|------------|-------|
| Add Order | O(log n) | O(1) with array-based price levels |
| Cancel Order | O(1) | Hash lookup + intrusive list removal |
| Modify Order | O(1) | Only reduces quantity, maintains priority |
| Match Order | O(m) | Where m = number of fills |
| Best Bid/Ask | O(1) | Cached |
| Order Lookup | O(1) | Hash map |

### Measured Performance

On modern hardware (tested on Intel i7-12700K, GCC 11, -O3):

- **Add Order**: ~100-200ns per order
- **Cancel Order**: ~50-100ns per cancel
- **Match Order**: ~200-400ns per trade
- **Order Lookup**: ~10-20ns per lookup
- **Throughput**: 2-5M orders/sec sustained

*Performance varies based on order book depth, price level count, and system load.*

### Memory Usage

- Order: ~128 bytes
- PriceLevel: ~64 bytes + orders
- OrderBook: ~1KB base + orders + price levels

## API Reference

### Context Management
```c
mx_context_t* mx_context_new(void);
void mx_context_free(mx_context_t* ctx);
void mx_context_set_callbacks(mx_context_t* ctx, 
                              mx_trade_callback_t trade_cb,
                              mx_order_callback_t order_cb,
                              void* user_data);
```

### Order Book Management
```c
mx_order_book_t* mx_order_book_new(mx_context_t* ctx, const char* symbol);
void mx_order_book_free(mx_order_book_t* book);
void mx_order_book_clear(mx_order_book_t* book);
```

### Order Operations
```c
// Simple API
int mx_order_book_add_limit(mx_order_book_t* book, uint64_t order_id,
                            mx_side_t side, uint32_t price, uint32_t quantity);
int mx_order_book_add_market(mx_order_book_t* book, uint64_t order_id,
                             mx_side_t side, uint32_t quantity);
int mx_order_book_cancel(mx_order_book_t* book, uint64_t order_id);
int mx_order_book_modify(mx_order_book_t* book, uint64_t order_id, 
                         uint32_t new_quantity);

// Advanced API
int mx_order_book_add_order(mx_order_book_t* book, uint64_t order_id,
                            mx_order_type_t type, mx_side_t side,
                            uint32_t price, uint32_t stop_price,
                            uint32_t quantity, uint32_t display_qty,
                            mx_time_in_force_t tif, uint32_t flags,
                            uint64_t expire_time);
```

### Market Data Queries
```c
uint32_t mx_order_book_get_best_bid(const mx_order_book_t* book);
uint32_t mx_order_book_get_best_ask(const mx_order_book_t* book);
uint32_t mx_order_book_get_spread(const mx_order_book_t* book);
uint32_t mx_order_book_get_mid_price(const mx_order_book_t* book);
uint64_t mx_order_book_get_depth(const mx_order_book_t* book,
                                 mx_side_t side, uint32_t num_levels);
```

See `include/matchengine.h` for complete API documentation.

## Project Structure
```
matchengine/
├── include/
│   ├── matchengine.h              # Public API (only include this)
│   └── internal/                  # Internal headers (do not include)
│       ├── common.h
│       ├── allocator.h
│       ├── types.h
│       ├── context.h
│       ├── core/
│       │   ├── order.h
│       │   ├── price_level.h
│       │   ├── order_pool.h
│       │   └── order_book.h
│       └── utils/
│           ├── intrusive_list.h
│           ├── memory_pool.h
│           └── hash_map.h
├── src/
│   ├── allocator.cpp
│   ├── version.cpp
│   ├── context.cpp
│   ├── api.cpp                    # C API shim layer
│   └── core/
│       └── order_book.cpp         # Matching engine core
├── examples/
│   ├── basic_usage.c
│   ├── advanced_usage.cpp
│   └── benchmark.cpp
├── tests/                         # Python CFFI tests
│   ├── testhelpers.py
│   ├── conftest.py
│   ├── test_basic.py
│   ├── test_matching.py
│   ├── test_price_time_priority.py
│   └── test_performance.py
├── premake5.lua                   # Build configuration
└── README.md
```

## Integration with Client-Server Architecture

This library is designed to be embedded in a larger trading system. Typical architecture:
```
┌──────────────┐         ┌──────────────┐         ┌──────────────┐
│   Clients    │────────▶│  Gateway/    │────────▶│   Matching   │
│  (Trading    │  TCP/   │  Order       │  IPC/   │   Engine     │
│   Apps)      │  WebSkt │  Router      │  Queue  │ (This Lib)   │
└──────────────┘         └──────────────┘         └──────────────┘
```

The matching engine library handles the core order matching logic. You'll typically build:
1. Network layer for client connections
2. Order routing and validation
3. Market data distribution
4. Persistence layer

See the parent project for a complete client-server implementation.

## Authors

Built following best practices from:
- *Beautiful Native Libraries* by Armin Ronacher
- Industry-standard matching engine design patterns
- High-frequency trading system architecture


