/**
 * MatchX - High Performance Order Matching Engine
 * Public C API Header
 * 
 * This is the ONLY header that users of the library should include.
 */

#ifndef MATCHENGINE_H_INCLUDED
#define MATCHENGINE_H_INCLUDED

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Version Information
 * ========================================================================= */
#define MX_VERSION_MAJOR 1
#define MX_VERSION_MINOR 0
#define MX_VERSION_PATCH 0
#define MX_VERSION ((MX_VERSION_MAJOR << 16) | (MX_VERSION_MINOR << 8) | MX_VERSION_PATCH)

MX_API unsigned int mx_get_version(void);
MX_API int mx_is_compatible_dll(void);

/* ============================================================================
 * Symbol Export/Import Configuration
 * ========================================================================= */
#ifndef MX_API
# ifdef _WIN32
#  if defined(MX_BUILD_SHARED)
#   define MX_API __declspec(dllexport)
#  elif !defined(MX_BUILD_STATIC)
#   define MX_API __declspec(dllimport)
#  else
#   define MX_API
#  endif
# else
#  if __GNUC__ >= 4
#   define MX_API __attribute__((visibility("default")))
#  else
#   define MX_API
#  endif
# endif
#endif

/* ============================================================================
 * Forward Declarations - Opaque Handles
 * ========================================================================= */
struct mx_context_s;
struct mx_order_book_s;

typedef struct mx_context_s mx_context_t;
typedef struct mx_order_book_s mx_order_book_t;

/* ============================================================================
 * Enumerations
 * ========================================================================= */

/* Order side */
typedef enum {
    MX_SIDE_BUY = 0,
    MX_SIDE_SELL = 1
} mx_side_t;

/* Order type */
typedef enum {
    MX_ORDER_TYPE_LIMIT = 0,
    MX_ORDER_TYPE_MARKET = 1,
    MX_ORDER_TYPE_STOP = 2,          /* Stop market order */
    MX_ORDER_TYPE_STOP_LIMIT = 3     /* Stop limit order */
} mx_order_type_t;

/* Time in force */
typedef enum {
    MX_TIF_GTC = 0,    /* Good Till Cancel - default */
    MX_TIF_IOC = 1,    /* Immediate or Cancel - match immediately, cancel rest */
    MX_TIF_FOK = 2,    /* Fill or Kill - all or nothing, immediate */
    MX_TIF_DAY = 3,    /* Good for Day - expires at end of trading day */
    MX_TIF_GTD = 4     /* Good Till Date - expires at specific timestamp */
} mx_time_in_force_t;

/* Order flags for advanced features */
typedef enum {
    MX_ORDER_FLAG_NONE = 0,
    MX_ORDER_FLAG_POST_ONLY = (1 << 0),     /* Reject if would match immediately */
    MX_ORDER_FLAG_HIDDEN = (1 << 1),        /* Hidden/Iceberg order */
    MX_ORDER_FLAG_AON = (1 << 2),           /* All-or-None */
    MX_ORDER_FLAG_REDUCE_ONLY = (1 << 3)    /* Can only reduce position */
} mx_order_flags_t;

/* Status codes */
typedef enum {
    MX_STATUS_OK = 0,
    MX_STATUS_ERROR = -1,
    MX_STATUS_INVALID_PARAM = -2,
    MX_STATUS_OUT_OF_MEMORY = -3,
    MX_STATUS_ORDER_NOT_FOUND = -4,
    MX_STATUS_INVALID_PRICE = -5,
    MX_STATUS_INVALID_QUANTITY = -6,
    MX_STATUS_DUPLICATE_ORDER = -7,
    MX_STATUS_WOULD_MATCH = -8,             /* POST_ONLY order would have matched */
    MX_STATUS_CANNOT_FILL = -9,             /* FOK/AON cannot be filled */
    MX_STATUS_STOP_NOT_TRIGGERED = -10      /* Stop order not triggered yet */
} mx_status_t;

/* Order event types for callbacks */
typedef enum {
    MX_EVENT_ORDER_ACCEPTED = 0,    /* Order added to book */
    MX_EVENT_ORDER_REJECTED = 1,    /* Order rejected */
    MX_EVENT_ORDER_FILLED = 2,      /* Order fully filled */
    MX_EVENT_ORDER_PARTIAL = 3,     /* Order partially filled */
    MX_EVENT_ORDER_CANCELLED = 4,   /* Order cancelled */
    MX_EVENT_ORDER_EXPIRED = 5,     /* Order expired (DAY/GTD) */
    MX_EVENT_ORDER_TRIGGERED = 6    /* Stop order triggered */
} mx_order_event_t;

/* ============================================================================
 * Callback Types
 * ========================================================================= */

/* Trade callback - called when orders match */
typedef void (*mx_trade_callback_t)(
    void* user_data,
    uint64_t aggressive_order_id,    /* Order that caused the match */
    uint64_t passive_order_id,       /* Resting order in book */
    uint32_t price,
    uint32_t quantity,
    uint64_t timestamp
);

/* Order event callback - called for order lifecycle events */
typedef void (*mx_order_callback_t)(
    void* user_data,
    uint64_t order_id,
    mx_order_event_t event,
    uint32_t filled_quantity,
    uint32_t remaining_quantity
);

/* ============================================================================
 * Memory Allocator Functions
 * ========================================================================= */

/**
 * Set custom memory allocators for the library.
 * Must be called before any other mx_ functions if you want custom allocation.
 * 
 * @param f_malloc  Custom malloc function
 * @param f_realloc Custom realloc function  
 * @param f_free    Custom free function
 */
MX_API void mx_set_allocators(
    void* (*f_malloc)(size_t),
    void* (*f_realloc)(void*, size_t),
    void (*f_free)(void*)
);

/* ============================================================================
 * Context Management
 * ========================================================================= */

/**
 * Create a new matching engine context.
 * The context holds configuration and can have per-context allocators.
 * 
 * @return Context handle, or NULL on failure
 */
MX_API mx_context_t* mx_context_new(void);

/**
 * Free a context and all associated resources.
 * 
 * @param ctx Context to free
 */
MX_API void mx_context_free(mx_context_t* ctx);

/**
 * Set callbacks for the context.
 * 
 * @param ctx           Context handle
 * @param trade_cb      Callback for trade events (can be NULL)
 * @param order_cb      Callback for order events (can be NULL)
 * @param user_data     User data passed to callbacks
 */
MX_API void mx_context_set_callbacks(
    mx_context_t* ctx,
    mx_trade_callback_t trade_cb,
    mx_order_callback_t order_cb,
    void* user_data
);

/**
 * Set the current timestamp for the context (used for DAY/GTD orders).
 * 
 * @param ctx       Context handle
 * @param timestamp Current timestamp in nanoseconds
 */
MX_API void mx_context_set_timestamp(mx_context_t* ctx, uint64_t timestamp);

/**
 * Get the current timestamp from the context.
 * 
 * @param ctx Context handle
 * @return Current timestamp in nanoseconds
 */
MX_API uint64_t mx_context_get_timestamp(const mx_context_t* ctx);

/* ============================================================================
 * Order Book Management
 * ========================================================================= */

/**
 * Create a new order book for a symbol.
 * 
 * @param ctx    Context handle
 * @param symbol Symbol name (will be copied)
 * @return Order book handle, or NULL on failure
 */
MX_API mx_order_book_t* mx_order_book_new(mx_context_t* ctx, const char* symbol);

/**
 * Free an order book and all its orders.
 * 
 * @param book Order book to free
 */
MX_API void mx_order_book_free(mx_order_book_t* book);

/**
 * Get the symbol for an order book.
 * 
 * @param book Order book
 * @return Symbol string (do not free)
 */
MX_API const char* mx_order_book_get_symbol(const mx_order_book_t* book);

/**
 * Get statistics about the order book.
 * 
 * @param book          Order book
 * @param total_orders  Output: total number of active orders (can be NULL)
 * @param bid_levels    Output: number of bid price levels (can be NULL)
 * @param ask_levels    Output: number of ask price levels (can be NULL)
 * @param total_bid_volume Output: total volume on bid side (can be NULL)
 * @param total_ask_volume Output: total volume on ask side (can be NULL)
 */
MX_API void mx_order_book_get_stats(
    const mx_order_book_t* book,
    uint32_t* total_orders,
    uint32_t* bid_levels,
    uint32_t* ask_levels,
    uint64_t* total_bid_volume,
    uint64_t* total_ask_volume
);

/**
 * Clear all orders from the order book.
 * 
 * @param book Order book
 */
MX_API void mx_order_book_clear(mx_order_book_t* book);

/* ============================================================================
 * Order Operations - Simple API
 * ========================================================================= */

/**
 * Add a simple limit order (GTC, no special flags).
 * This will immediately match against existing orders if possible.
 * 
 * @param book     Order book
 * @param order_id Unique order ID
 * @param side     Buy or sell
 * @param price    Price in ticks (integer)
 * @param quantity Quantity in lots
 * @return MX_STATUS_OK on success, error code otherwise
 */
MX_API int mx_order_book_add_limit(
    mx_order_book_t* book,
    uint64_t order_id,
    mx_side_t side,
    uint32_t price,
    uint32_t quantity
);

/**
 * Add a simple market order.
 * Market orders execute immediately at best available price.
 * 
 * @param book     Order book
 * @param order_id Unique order ID
 * @param side     Buy or sell
 * @param quantity Quantity in lots
 * @return MX_STATUS_OK on success, error code otherwise
 */
MX_API int mx_order_book_add_market(
    mx_order_book_t* book,
    uint64_t order_id,
    mx_side_t side,
    uint32_t quantity
);

/* ============================================================================
 * Order Operations - Advanced API
 * ========================================================================= */

/**
 * Add an order with full control over all parameters.
 * 
 * @param book          Order book
 * @param order_id      Unique order ID
 * @param order_type    Order type (limit, market, stop, stop-limit)
 * @param side          Buy or sell
 * @param price         Limit price in ticks (0 for market orders)
 * @param stop_price    Stop trigger price (0 for non-stop orders)
 * @param quantity      Total order quantity
 * @param display_qty   Visible quantity for iceberg (0 = show all)
 * @param tif           Time in force
 * @param flags         Order flags (can be OR'd together)
 * @param expire_time   Expiration timestamp for GTD (0 = no expiry)
 * @return MX_STATUS_OK on success, error code otherwise
 */
MX_API int mx_order_book_add_order(
    mx_order_book_t* book,
    uint64_t order_id,
    mx_order_type_t order_type,
    mx_side_t side,
    uint32_t price,
    uint32_t stop_price,
    uint32_t quantity,
    uint32_t display_qty,
    mx_time_in_force_t tif,
    uint32_t flags,
    uint64_t expire_time
);

/**
 * Cancel an order from the order book.
 * 
 * @param book     Order book
 * @param order_id Order ID to cancel
 * @return MX_STATUS_OK on success, MX_STATUS_ORDER_NOT_FOUND if not found
 */
MX_API int mx_order_book_cancel(
    mx_order_book_t* book,
    uint64_t order_id
);

/**
 * Modify an order's quantity (only reduces quantity, maintains time priority).
 * 
 * @param book         Order book
 * @param order_id     Order ID to modify
 * @param new_quantity New quantity (must be less than current)
 * @return MX_STATUS_OK on success, error code otherwise
 */
MX_API int mx_order_book_modify(
    mx_order_book_t* book,
    uint64_t order_id,
    uint32_t new_quantity
);

/**
 * Replace an order (cancel and replace - loses time priority).
 * 
 * @param book          Order book
 * @param old_order_id  Order ID to replace
 * @param new_order_id  New order ID
 * @param new_price     New price
 * @param new_quantity  New quantity
 * @return MX_STATUS_OK on success, error code otherwise
 */
MX_API int mx_order_book_replace(
    mx_order_book_t* book,
    uint64_t old_order_id,
    uint64_t new_order_id,
    uint32_t new_price,
    uint32_t new_quantity
);

/* ============================================================================
 * Market Data Queries
 * ========================================================================= */

/**
 * Get the best bid price.
 * 
 * @param book Order book
 * @return Best bid price, or 0 if no bids
 */
MX_API uint32_t mx_order_book_get_best_bid(const mx_order_book_t* book);

/**
 * Get the best ask price.
 * 
 * @param book Order book
 * @return Best ask price, or 0 if no asks
 */
MX_API uint32_t mx_order_book_get_best_ask(const mx_order_book_t* book);

/**
 * Get the spread (difference between best ask and best bid).
 * 
 * @param book Order book
 * @return Spread in ticks, or 0 if no market
 */
MX_API uint32_t mx_order_book_get_spread(const mx_order_book_t* book);

/**
 * Get volume at a specific price level.
 * 
 * @param book  Order book
 * @param side  Bid or ask side
 * @param price Price level to query
 * @return Total volume at that price, or 0 if none
 */
MX_API uint32_t mx_order_book_get_volume_at_price(
    const mx_order_book_t* book,
    mx_side_t side,
    uint32_t price
);

/**
 * Get the mid price (average of best bid and best ask).
 * 
 * @param book Order book
 * @return Mid price, or 0 if no market
 */
MX_API uint32_t mx_order_book_get_mid_price(const mx_order_book_t* book);

/**
 * Get market depth - aggregate volume across multiple price levels.
 * 
 * @param book       Order book
 * @param side       Bid or ask side
 * @param num_levels Number of price levels to aggregate
 * @return Total volume across those levels
 */
MX_API uint64_t mx_order_book_get_depth(
    const mx_order_book_t* book,
    mx_side_t side,
    uint32_t num_levels
);

/* ============================================================================
 * Order Queries
 * ========================================================================= */

/**
 * Check if an order exists in the book.
 * 
 * @param book     Order book
 * @param order_id Order ID to check
 * @return 1 if exists, 0 if not
 */
MX_API int mx_order_book_has_order(
    const mx_order_book_t* book,
    uint64_t order_id
);

/**
 * Get information about a specific order.
 * 
 * @param book       Order book
 * @param order_id   Order ID to query
 * @param side       Output: order side (can be NULL)
 * @param price      Output: order price (can be NULL)
 * @param quantity   Output: remaining quantity (can be NULL)
 * @param filled     Output: filled quantity (can be NULL)
 * @return MX_STATUS_OK if found, MX_STATUS_ORDER_NOT_FOUND if not
 */
MX_API int mx_order_book_get_order_info(
    const mx_order_book_t* book,
    uint64_t order_id,
    mx_side_t* side,
    uint32_t* price,
    uint32_t* quantity,
    uint32_t* filled
);

/* ============================================================================
 * Administrative Functions
 * ========================================================================= */

/**
 * Process expired orders (DAY/GTD).
 * Should be called periodically if using time-based orders.
 * 
 * @param book      Order book
 * @param timestamp Current timestamp
 * @return Number of orders expired
 */
MX_API uint32_t mx_order_book_process_expirations(
    mx_order_book_t* book,
    uint64_t timestamp
);

/**
 * Trigger stop orders based on market price movement.
 * Should be called after trades when stop orders might be triggered.
 * 
 * @param book Order book
 * @return Number of stop orders triggered
 */
MX_API uint32_t mx_order_book_process_stops(mx_order_book_t* book);

/* ============================================================================
 * Utility Functions
 * ========================================================================= */

/**
 * Get a human-readable error message for a status code.
 * 
 * @param status Status code
 * @return Error message string (do not free)
 */
MX_API const char* mx_status_message(mx_status_t status);

/**
 * Get a human-readable name for an order type.
 * 
 * @param type Order type
 * @return Type name string (do not free)
 */
MX_API const char* mx_order_type_name(mx_order_type_t type);

/**
 * Get a human-readable name for time-in-force.
 * 
 * @param tif Time in force
 * @return TIF name string (do not free)
 */
MX_API const char* mx_tif_name(mx_time_in_force_t tif);

#ifdef __cplusplus
}
#endif

#endif /* MATCHENGINE_H_INCLUDED */
