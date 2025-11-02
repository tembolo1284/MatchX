/**
 * C API implementation - the shim layer
 * Bridges between C API and C++ implementation
 * Following the article's reinterpret_cast pattern
 */

#include "matchengine.h"
#include "internal/context.h"
#include "internal/core/order_book.h"
#include "internal/allocator.h"

extern "C" {
/* ============================================================================
 * Type Casting Macros (as per the article)
 * ========================================================================= */

#define AS_TYPE(Type, Obj) reinterpret_cast<Type*>(Obj)
#define AS_CTYPE(Type, Obj) reinterpret_cast<const Type*>(Obj)

/* ============================================================================
 * Order Book Management
 * ========================================================================= */

mx_order_book_t* mx_order_book_new(mx_context_t* ctx, const char* symbol) {
    if (!ctx || !symbol) return nullptr;
    
    matchx::Context* context = AS_TYPE(matchx::Context, ctx);
    matchx::OrderBook* book = new matchx::OrderBook(context, symbol);
    
    return reinterpret_cast<mx_order_book_t*>(book);
}

void mx_order_book_free(mx_order_book_t* book) {
    if (!book) return;
    
    matchx::OrderBook* orderbook = AS_TYPE(matchx::OrderBook, book);
    delete orderbook;
}

const char* mx_order_book_get_symbol(const mx_order_book_t* book) {
    if (!book) return nullptr;
    
    const matchx::OrderBook* orderbook = AS_CTYPE(matchx::OrderBook, book);
    return orderbook->symbol();
}

void mx_order_book_get_stats(const mx_order_book_t* book,
                             uint32_t* total_orders,
                             uint32_t* bid_levels,
                             uint32_t* ask_levels,
                             uint64_t* total_bid_volume,
                             uint64_t* total_ask_volume) {
    if (!book) return;
    
    const matchx::OrderBook* orderbook = AS_CTYPE(matchx::OrderBook, book);
    matchx::OrderBookStats stats = orderbook->get_stats();
    
    if (total_orders) *total_orders = stats.total_orders;
    if (bid_levels) *bid_levels = stats.bid_levels;
    if (ask_levels) *ask_levels = stats.ask_levels;
    if (total_bid_volume) *total_bid_volume = stats.total_bid_volume;
    if (total_ask_volume) *total_ask_volume = stats.total_ask_volume;
}

void mx_order_book_clear(mx_order_book_t* book) {
    if (!book) return;
    
    matchx::OrderBook* orderbook = AS_TYPE(matchx::OrderBook, book);
    orderbook->clear();
}

/* ============================================================================
 * Simple Order Operations
 * ========================================================================= */

int mx_order_book_add_limit(mx_order_book_t* book,
                            uint64_t order_id,
                            mx_side_t side,
                            uint32_t price,
                            uint32_t quantity) {
    if (!book) return MX_STATUS_INVALID_PARAM;
    
    matchx::OrderBook* orderbook = AS_TYPE(matchx::OrderBook, book);
    return orderbook->add_limit_order(order_id, side, price, quantity);
}

int mx_order_book_add_market(mx_order_book_t* book,
                             uint64_t order_id,
                             mx_side_t side,
                             uint32_t quantity) {
    if (!book) return MX_STATUS_INVALID_PARAM;
    
    matchx::OrderBook* orderbook = AS_TYPE(matchx::OrderBook, book);
    return orderbook->add_market_order(order_id, side, quantity);
}

int mx_order_book_cancel(mx_order_book_t* book, uint64_t order_id) {
    if (!book) return MX_STATUS_INVALID_PARAM;
    
    matchx::OrderBook* orderbook = AS_TYPE(matchx::OrderBook, book);
    return orderbook->cancel_order(order_id);
}

int mx_order_book_modify(mx_order_book_t* book,
                         uint64_t order_id,
                         uint32_t new_quantity) {
    if (!book) return MX_STATUS_INVALID_PARAM;
    
    matchx::OrderBook* orderbook = AS_TYPE(matchx::OrderBook, book);
    return orderbook->modify_order(order_id, new_quantity);
}

int mx_order_book_replace(mx_order_book_t* book,
                          uint64_t old_order_id,
                          uint64_t new_order_id,
                          uint32_t new_price,
                          uint32_t new_quantity) {
    if (!book) return MX_STATUS_INVALID_PARAM;
    
    matchx::OrderBook* orderbook = AS_TYPE(matchx::OrderBook, book);
    return orderbook->replace_order(old_order_id, new_order_id, new_price, new_quantity);
}

/* ============================================================================
 * Advanced Order Operations
 * ========================================================================= */

int mx_order_book_add_order(mx_order_book_t* book,
                            uint64_t order_id,
                            mx_order_type_t order_type,
                            mx_side_t side,
                            uint32_t price,
                            uint32_t stop_price,
                            uint32_t quantity,
                            uint32_t display_qty,
                            mx_time_in_force_t tif,
                            uint32_t flags,
                            uint64_t expire_time) {
    if (!book) return MX_STATUS_INVALID_PARAM;
    
    matchx::OrderBook* orderbook = AS_TYPE(matchx::OrderBook, book);
    return orderbook->add_order(order_id, order_type, side, price, stop_price,
                               quantity, display_qty, tif, flags, expire_time);
}

/* ============================================================================
 * Market Data Queries
 * ========================================================================= */

uint32_t mx_order_book_get_best_bid(const mx_order_book_t* book) {
    if (!book) return 0;
    
    const matchx::OrderBook* orderbook = AS_CTYPE(matchx::OrderBook, book);
    return orderbook->get_best_bid();
}

uint32_t mx_order_book_get_best_ask(const mx_order_book_t* book) {
    if (!book) return 0;
    
    const matchx::OrderBook* orderbook = AS_CTYPE(matchx::OrderBook, book);
    return orderbook->get_best_ask();
}

uint32_t mx_order_book_get_spread(const mx_order_book_t* book) {
    if (!book) return 0;
    
    const matchx::OrderBook* orderbook = AS_CTYPE(matchx::OrderBook, book);
    return orderbook->get_spread();
}

uint32_t mx_order_book_get_volume_at_price(const mx_order_book_t* book,
                                           mx_side_t side,
                                           uint32_t price) {
    if (!book) return 0;
    
    const matchx::OrderBook* orderbook = AS_CTYPE(matchx::OrderBook, book);
    return orderbook->get_volume_at_price(side, price);
}

uint32_t mx_order_book_get_mid_price(const mx_order_book_t* book) {
    if (!book) return 0;
    
    const matchx::OrderBook* orderbook = AS_CTYPE(matchx::OrderBook, book);
    return orderbook->get_mid_price();
}

uint64_t mx_order_book_get_depth(const mx_order_book_t* book,
                                 mx_side_t side,
                                 uint32_t num_levels) {
    if (!book) return 0;
    
    const matchx::OrderBook* orderbook = AS_CTYPE(matchx::OrderBook, book);
    return orderbook->get_depth(side, num_levels);
}

/* ============================================================================
 * Order Queries
 * ========================================================================= */

int mx_order_book_has_order(const mx_order_book_t* book, uint64_t order_id) {
    if (!book) return 0;
    
    const matchx::OrderBook* orderbook = AS_CTYPE(matchx::OrderBook, book);
    return orderbook->has_order(order_id) ? 1 : 0;
}

int mx_order_book_get_order_info(const mx_order_book_t* book,
                                 uint64_t order_id,
                                 mx_side_t* side,
                                 uint32_t* price,
                                 uint32_t* quantity,
                                 uint32_t* filled) {
    if (!book) return MX_STATUS_INVALID_PARAM;
    
    const matchx::OrderBook* orderbook = AS_CTYPE(matchx::OrderBook, book);
    matchx::OrderSnapshot snapshot;
    
    if (!orderbook->get_order_info(order_id, snapshot)) {
        return MX_STATUS_ORDER_NOT_FOUND;
    }
    
    if (side) *side = snapshot.side;
    if (price) *price = snapshot.price;
    if (quantity) *quantity = snapshot.remaining_quantity;
    if (filled) *filled = snapshot.filled_quantity;
    
    return MX_STATUS_OK;
}

/* ============================================================================
 * Administrative Functions
 * ========================================================================= */

uint32_t mx_order_book_process_expirations(mx_order_book_t* book,
                                           uint64_t timestamp) {
    if (!book) return 0;
    
    matchx::OrderBook* orderbook = AS_TYPE(matchx::OrderBook, book);
    return orderbook->process_expirations(timestamp);
}

uint32_t mx_order_book_process_stops(mx_order_book_t* book) {
    if (!book) return 0;
    
    matchx::OrderBook* orderbook = AS_TYPE(matchx::OrderBook, book);
    return orderbook->process_stops();
}

} // extern "C"
