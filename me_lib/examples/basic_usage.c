/**
 * Basic Usage Example - Simple C example
 * Demonstrates basic order matching
 */

#include "matchengine.h"
#include <stdio.h>
#include <stdlib.h>

/* Trade callback - called when orders match */
void on_trade(void* user_data, uint64_t buy_id, uint64_t sell_id,
              uint32_t price, uint32_t quantity, uint64_t timestamp) {
    (void)user_data;
    (void)timestamp;
    
    printf("  TRADE: Buy #%llu × Sell #%llu @ $%.2f for %u shares\n",
           (unsigned long long)buy_id,
           (unsigned long long)sell_id,
           price / 100.0,
           quantity);
}

/* Order event callback */
void on_order_event(void* user_data, uint64_t order_id, mx_order_event_t event,
                   uint32_t filled_qty, uint32_t remaining_qty) {
    (void)user_data;
    
    const char* event_str = "";
    switch (event) {
        case 0: event_str = "ACCEPTED"; break;
        case 1: event_str = "REJECTED"; break;
        case 2: event_str = "FILLED"; break;
        case 3: event_str = "PARTIAL"; break;
        case 4: event_str = "CANCELLED"; break;
        case 5: event_str = "EXPIRED"; break;
        case 6: event_str = "TRIGGERED"; break;
    }
    
    printf("  ORDER #%llu: %s (filled: %u, remaining: %u)\n",
           (unsigned long long)order_id, event_str, filled_qty, remaining_qty);
}

int main(void) {
    printf("MatchX Matching Engine - Basic Usage Example\n");
    printf("=============================================\n\n");
    
    /* Check library version */
    unsigned int version = mx_get_version();
    unsigned int major = (version >> 16) & 0xFF;
    unsigned int minor = (version >> 8) & 0xFF;
    unsigned int patch = version & 0xFF;
    
    printf("Library Version: %u.%u.%u\n", major, minor, patch);
    
    if (!mx_is_compatible_dll()) {
        printf("ERROR: Incompatible library version!\n");
        return 1;
    }
    
    printf("Version check: OK\n\n");
    
    /* Create context */
    mx_context_t* ctx = mx_context_new();
    if (!ctx) {
        printf("ERROR: Failed to create context\n");
        return 1;
    }
    
    /* Set callbacks */
    mx_context_set_callbacks(ctx, on_trade, on_order_event, NULL);
    
    /* Create order book for AAPL */
    mx_order_book_t* book = mx_order_book_new(ctx, "AAPL");
    if (!book) {
        printf("ERROR: Failed to create order book\n");
        mx_context_free(ctx);
        return 1;
    }
    
    printf("Order Book Created: AAPL\n\n");
    
    /* Example 1: Simple match */
    printf("Example 1: Simple Match\n");
    printf("-----------------------\n");
    
    printf("Adding sell order: 100 shares @ $150.00\n");
    mx_order_book_add_limit(book, 1, MX_SIDE_SELL, 15000, 100);
    
    printf("Adding buy order:  100 shares @ $150.00\n");
    mx_order_book_add_limit(book, 2, MX_SIDE_BUY, 15000, 100);
    
    printf("\n");
    
    /* Example 2: Partial fill */
    printf("Example 2: Partial Fill\n");
    printf("-----------------------\n");
    
    printf("Adding sell order: 200 shares @ $151.00\n");
    mx_order_book_add_limit(book, 3, MX_SIDE_SELL, 15100, 200);
    
    printf("Adding buy order:  75 shares @ $151.00\n");
    mx_order_book_add_limit(book, 4, MX_SIDE_BUY, 15100, 75);
    
    printf("\n");
    
    /* Example 3: Market order */
    printf("Example 3: Market Order\n");
    printf("-----------------------\n");
    
    printf("Adding market buy: 50 shares\n");
    mx_order_book_add_market(book, 5, MX_SIDE_BUY, 50);
    
    printf("\n");
    
    /* Example 4: Build order book */
    printf("Example 4: Building Order Book\n");
    printf("-------------------------------\n");
    
    /* Add bid ladder */
    mx_order_book_add_limit(book, 10, MX_SIDE_BUY, 14950, 100);  /* $149.50 */
    mx_order_book_add_limit(book, 11, MX_SIDE_BUY, 14900, 150);  /* $149.00 */
    mx_order_book_add_limit(book, 12, MX_SIDE_BUY, 14850, 200);  /* $148.50 */
    
    /* Add ask ladder */
    mx_order_book_add_limit(book, 20, MX_SIDE_SELL, 15200, 100); /* $152.00 */
    mx_order_book_add_limit(book, 21, MX_SIDE_SELL, 15250, 150); /* $152.50 */
    mx_order_book_add_limit(book, 22, MX_SIDE_SELL, 15300, 200); /* $153.00 */
    
    printf("\n");
    
    /* Query market data */
    printf("Market Data:\n");
    uint32_t best_bid = mx_order_book_get_best_bid(book);
    uint32_t best_ask = mx_order_book_get_best_ask(book);
    uint32_t spread = mx_order_book_get_spread(book);
    uint32_t mid = mx_order_book_get_mid_price(book);
    
    printf("  Best Bid:  $%.2f\n", best_bid / 100.0);
    printf("  Best Ask:  $%.2f\n", best_ask / 100.0);
    printf("  Spread:    $%.2f\n", spread / 100.0);
    printf("  Mid Price: $%.2f\n", mid / 100.0);
    
    printf("\n");
    
    /* Get statistics */
    uint32_t total_orders = 0;
    uint32_t bid_levels = 0;
    uint32_t ask_levels = 0;
    
    mx_order_book_get_stats(book, &total_orders, &bid_levels, &ask_levels, NULL, NULL);
    
    printf("Order Book Statistics:\n");
    printf("  Total Orders: %u\n", total_orders);
    printf("  Bid Levels:   %u\n", bid_levels);
    printf("  Ask Levels:   %u\n", ask_levels);
    
    printf("\n");
    
    /* Example 5: Cancel order */
    printf("Example 5: Cancel Order\n");
    printf("-----------------------\n");
    
    printf("Cancelling order #10\n");
    int result = mx_order_book_cancel(book, 10);
    printf("Cancel result: %s\n", mx_status_message(result));
    
    /* Check updated best bid */
    best_bid = mx_order_book_get_best_bid(book);
    printf("New Best Bid: $%.2f\n", best_bid / 100.0);
    
    printf("\n");
    
    /* Cleanup */
    mx_order_book_free(book);
    mx_context_free(ctx);
    
    printf("Example complete!\n");
    
    return 0;
}
