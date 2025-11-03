/**
 * OrderBook implementation - the matching engine core
 * This is where the magic happens!
 */

#include "internal/core/order_book.h"
#include "internal/context.h"
#include "internal/allocator.h"
#include <algorithm>

namespace matchx {

/* ============================================================================
 * Constructor / Destructor
 * ========================================================================= */

OrderBook::OrderBook(Context* ctx, const char* symbol)
    : symbol_(nullptr)
    , context_(ctx)
    , order_pool_(ctx->config().expected_max_orders)
    , bid_levels_()
    , ask_levels_()
    , stop_orders_()
    , best_bid_(0)
    , best_ask_(0)
    , total_trades_(0)
    , total_volume_(0) {
    
    // Copy symbol string
    if (symbol) {
        symbol_ = mx_strdup(symbol);
    }
    
    // Reserve capacity in stop orders map
    stop_orders_.reserve(1000);
}

OrderBook::~OrderBook() {
    clear();
    
    if (symbol_) {
        mx_free(symbol_);
        symbol_ = nullptr;
    }
}

/* ============================================================================
 * Simple Order Operations
 * ========================================================================= */

mx_status_t OrderBook::add_limit_order(OrderId order_id, Side side,
                                       Price price, Quantity quantity) {
    // Validate parameters
    if (order_id == INVALID_ORDER_ID) return MX_STATUS_INVALID_PARAM;
    if (price == 0) return MX_STATUS_INVALID_PRICE;
    if (quantity == 0) return MX_STATUS_INVALID_QUANTITY;
    
    // Check for duplicate
    if (order_pool_.has_order(order_id)) {
        return MX_STATUS_DUPLICATE_ORDER;
    }
    
    // Create order
    Timestamp now = get_current_timestamp();
    Order* order = order_pool_.create_order(order_id, side, price, quantity, now);
    
    if (!order) {
        return MX_STATUS_OUT_OF_MEMORY;
    }
    
    // Process the order
    return process_new_order(order);
}

mx_status_t OrderBook::add_market_order(OrderId order_id, Side side, Quantity quantity) {
    if (order_id == INVALID_ORDER_ID) return MX_STATUS_INVALID_PARAM;
    if (quantity == 0) return MX_STATUS_INVALID_QUANTITY;
    
    if (order_pool_.has_order(order_id)) {
        return MX_STATUS_DUPLICATE_ORDER;
    }
    
    Timestamp now = get_current_timestamp();
    Order* order = order_pool_.create_market_order(order_id, side, quantity, now);
    
    if (!order) {
        return MX_STATUS_OUT_OF_MEMORY;
    }
    
    return process_new_order(order);
}

mx_status_t OrderBook::cancel_order(OrderId order_id) {
    Order* order = order_pool_.find_order(order_id);
    
    if (!order) {
        return MX_STATUS_ORDER_NOT_FOUND;
    }
    
    // Remove from book or stop orders
    if (order->is_stop() && order->state() == OrderState::PENDING_NEW) {
        stop_orders_.erase(order_id);
    } else {
        remove_from_book(order);
    }
    
    // Mark as cancelled
    order->cancel();
    
    // Notify callback
    notify_order_event(order_id, MX_EVENT_ORDER_CANCELLED, 
                      order->filled_quantity(), 0);
    
    // Destroy order
    order_pool_.destroy_order(order);
    
    return MX_STATUS_OK;
}

mx_status_t OrderBook::modify_order(OrderId order_id, Quantity new_quantity) {
    Order* order = order_pool_.find_order(order_id);
    
    if (!order) {
        return MX_STATUS_ORDER_NOT_FOUND;
    }
    
    // Can only reduce quantity
    if (new_quantity >= order->total_quantity()) {
        return MX_STATUS_INVALID_QUANTITY;
    }
    
    // Cannot reduce below filled quantity
    if (new_quantity <= order->filled_quantity()) {
        return MX_STATUS_INVALID_QUANTITY;
    }
    
    // For orders in book, update the price level
    if (order->is_active() || order->is_partially_filled()) {
        PriceLevel* level = get_level(order->side(), order->price());
        if (level) {
            Quantity old_remaining = order->remaining_quantity();
            Quantity old_visible = order->visible_quantity();
            
            // Reduce quantity (maintains time priority!)
            order->reduce_quantity(new_quantity);
            
            // Update price level volumes
            level->update_order_volume(order, old_remaining, old_visible);
        }
    } else {
        // Just reduce quantity for non-active orders
        order->reduce_quantity(new_quantity);
    }
    
    return MX_STATUS_OK;
}

mx_status_t OrderBook::replace_order(OrderId old_order_id, OrderId new_order_id,
                                     Price new_price, Quantity new_quantity) {
    // Cancel old order
    mx_status_t status = cancel_order(old_order_id);
    if (status != MX_STATUS_OK) {
        return status;
    }
    
    // Get old order's side before it's destroyed
    Order* old_order = order_pool_.find_order(old_order_id);
    if (!old_order) {
        return MX_STATUS_ORDER_NOT_FOUND;
    }
    
    Side side = old_order->side();
    
    // Add new order (loses time priority)
    return add_limit_order(new_order_id, side, new_price, new_quantity);
}

/* ============================================================================
 * Advanced Order Operations
 * ========================================================================= */

mx_status_t OrderBook::add_order(OrderId order_id, OrderType order_type, Side side,
                                 Price price, Price stop_price, Quantity quantity,
                                 Quantity display_qty, TimeInForce tif, uint32_t flags,
                                 uint64_t expire_time) {
    
    // Validate
    mx_status_t status = validate_order(order_id, order_type, side, price, 
                                       stop_price, quantity, tif, flags);
    if (status != MX_STATUS_OK) {
        return status;
    }
    
    // Create order
    Timestamp now = get_current_timestamp();
    Order* order = order_pool_.create_order_full(
        order_id, order_type, side, price, stop_price,
        quantity, display_qty, tif, flags, now, expire_time
    );
    
    if (!order) {
        return MX_STATUS_OUT_OF_MEMORY;
    }
    
    // Handle stop orders separately
    if (order->is_stop()) {
        return handle_stop_order(order);
    }
    
    // Process regular order
    return process_new_order(order);
}

/* ============================================================================
 * Internal Order Processing
 * ========================================================================= */

mx_status_t OrderBook::process_new_order(Order* order) {
    MX_ASSERT(order != nullptr);
    
    // Check POST_ONLY flag
    if (order->is_post_only() && would_match_immediately(order)) {
        order->reject();
        notify_order_event(order->order_id(), MX_EVENT_ORDER_REJECTED, 0, 0);
        order_pool_.destroy_order(order);
        return MX_STATUS_WOULD_MATCH;
    }
    
    // Handle special TIF types
    if (order->is_fok()) {
        MatchResult result = handle_fok_order(order);
        return result.status;
    }
    
    if (order->is_ioc()) {
        MatchResult result = handle_ioc_order(order);
        return result.status;
    }
    
    // Regular matching
    MatchResult result = match_order(order);
    
    // Market orders never go in the book - always IOC behavior
    if (order->is_market()) {
        if (order->remaining_quantity() > 0) {
            // Market order couldn't be fully filled - cancel remainder
            order->cancel();
            notify_order_event(order->order_id(), MX_EVENT_ORDER_CANCELLED,
                             order->filled_quantity(), 0);
        } else {
            // Fully filled
            notify_order_event(order->order_id(), MX_EVENT_ORDER_FILLED,
                             order->filled_quantity(), 0);
        }
        order_pool_.destroy_order(order);
        return result.status;
    }
    
    // If order has remaining quantity and is GTC/DAY/GTD, add to book
    if (order->remaining_quantity() > 0 && !order->is_filled()) {
        if (order->is_gtc() || order->is_day() || order->is_gtd()) {
            add_to_book(order);
            
            if (order->filled_quantity() > 0) {
                notify_order_event(order->order_id(), MX_EVENT_ORDER_PARTIAL,
                                 order->filled_quantity(), order->remaining_quantity());
            } else {
                notify_order_event(order->order_id(), MX_EVENT_ORDER_ACCEPTED,
                                 0, order->remaining_quantity());
            }
        } else {
            // IOC-like behavior for other TIF types - cancel remainder
            order->cancel();
            notify_order_event(order->order_id(), MX_EVENT_ORDER_CANCELLED,
                             order->filled_quantity(), 0);
            order_pool_.destroy_order(order);
        }
    } else if (order->is_filled()) {
        notify_order_event(order->order_id(), MX_EVENT_ORDER_FILLED,
                         order->filled_quantity(), 0);
        order_pool_.destroy_order(order);
    }
    
    return result.status;
}

MatchResult OrderBook::match_limit_order(Order* order) {
    MatchResult result;
    result.status = MX_STATUS_OK;
    
    Timestamp now = get_current_timestamp();
    
    if (order->is_buy()) {
        // Buy order matches against asks (ascending)
        auto it = ask_levels_.begin();
        while (it != ask_levels_.end() && order->remaining_quantity() > 0) {
            PriceLevel& level = it->second;
            
            // Check if price allows matching
            if (order->price() < level.price()) {
                break; // No more matchable prices
            }
            
            // Collect filled orders for cleanup
            std::vector<OrderId> filled_orders;
            
            // Match at this level
            Quantity matched = level.match_orders(
                order, 
                order->remaining_quantity(),
                [this, now, &filled_orders](OrderId agg_id, OrderId pass_id, Price price, Quantity qty, Timestamp ts) {
                    this->notify_trade(agg_id, pass_id, price, qty, now);
                    
                    // Check passive order state after fill
                    Order* passive_order = order_pool_.find_order(pass_id);
                    if (passive_order) {
                        if (passive_order->is_filled()) {
                            filled_orders.push_back(pass_id);
                        } else if (passive_order->filled_quantity() > 0) {
                            // Passive order partially filled - notify
                            notify_order_event(pass_id, MX_EVENT_ORDER_PARTIAL,
                                             passive_order->filled_quantity(),
                                             passive_order->remaining_quantity());
                        }
                    }
                },
                now
            );
            
            result.matched_quantity += matched;
            total_trades_++;
            total_volume_ += matched;
            
            // Destroy fully filled passive orders (already removed from level by match_orders)
            for (OrderId filled_id : filled_orders) {
                Order* filled_order = order_pool_.find_order(filled_id);
                if (filled_order) {
                    notify_order_event(filled_id, MX_EVENT_ORDER_FILLED,
                                     filled_order->filled_quantity(), 0);
                    order_pool_.destroy_order(filled_order);
                }
            }
            
            // Remove level if empty
            if (level.empty()) {
                it = ask_levels_.erase(it);
                update_best_ask();
            } else {
                ++it;
            }
        }
    } else {
        // Sell order matches against bids (descending)
        auto it = bid_levels_.begin();
        while (it != bid_levels_.end() && order->remaining_quantity() > 0) {
            PriceLevel& level = it->second;
            
            // Check if price allows matching
            if (order->price() > level.price()) {
                break; // No more matchable prices
            }
            
            // Collect filled orders for cleanup
            std::vector<OrderId> filled_orders;
            
            // Match at this level
            Quantity matched = level.match_orders(
                order,
                order->remaining_quantity(),
                [this, now, &filled_orders](OrderId agg_id, OrderId pass_id, Price price, Quantity qty, Timestamp ts) {
                    this->notify_trade(agg_id, pass_id, price, qty, now);
                    
                    // Check passive order state after fill
                    Order* passive_order = order_pool_.find_order(pass_id);
                    if (passive_order) {
                        if (passive_order->is_filled()) {
                            filled_orders.push_back(pass_id);
                        } else if (passive_order->filled_quantity() > 0) {
                            // Passive order partially filled - notify
                            notify_order_event(pass_id, MX_EVENT_ORDER_PARTIAL,
                                             passive_order->filled_quantity(),
                                             passive_order->remaining_quantity());
                        }
                    }
                },
                now
            );
            
            result.matched_quantity += matched;
            total_trades_++;
            total_volume_ += matched;
            
            // Destroy fully filled passive orders (already removed from level by match_orders)
            for (OrderId filled_id : filled_orders) {
                Order* filled_order = order_pool_.find_order(filled_id);
                if (filled_order) {
                    notify_order_event(filled_id, MX_EVENT_ORDER_FILLED,
                                     filled_order->filled_quantity(), 0);
                    order_pool_.destroy_order(filled_order);
                }
            }
            
            // Remove level if empty
            if (level.empty()) {
                it = bid_levels_.erase(it);
                update_best_bid();
            } else {
                ++it;
            }
        }
    }
    
    result.remaining_quantity = order->remaining_quantity();
    result.fully_matched = (result.remaining_quantity == 0);
    
    return result;
}

MatchResult OrderBook::match_market_order(Order* order) {
    MatchResult result;
    result.status = MX_STATUS_OK;
    
    Timestamp now = get_current_timestamp();
    
    if (order->is_buy()) {
        // Market buy matches against asks
        auto it = ask_levels_.begin();
        while (it != ask_levels_.end() && order->remaining_quantity() > 0) {
            PriceLevel& level = it->second;
            
            // Collect filled orders for cleanup
            std::vector<OrderId> filled_orders;
            
            Quantity matched = level.match_orders(
                order,
                order->remaining_quantity(),
                [this, now, &filled_orders](OrderId agg_id, OrderId pass_id, Price price, Quantity qty, Timestamp ts) {
                    this->notify_trade(agg_id, pass_id, price, qty, now);
                    
                    // Check passive order state after fill
                    Order* passive_order = order_pool_.find_order(pass_id);
                    if (passive_order) {
                        if (passive_order->is_filled()) {
                            filled_orders.push_back(pass_id);
                        } else if (passive_order->filled_quantity() > 0) {
                            // Passive order partially filled - notify
                            notify_order_event(pass_id, MX_EVENT_ORDER_PARTIAL,
                                             passive_order->filled_quantity(),
                                             passive_order->remaining_quantity());
                        }
                    }
                },
                now
            );
            
            result.matched_quantity += matched;
            total_trades_++;
            total_volume_ += matched;
            
            // Destroy fully filled passive orders (already removed from level by match_orders)
            for (OrderId filled_id : filled_orders) {
                Order* filled_order = order_pool_.find_order(filled_id);
                if (filled_order) {
                    notify_order_event(filled_id, MX_EVENT_ORDER_FILLED,
                                     filled_order->filled_quantity(), 0);
                    order_pool_.destroy_order(filled_order);
                }
            }
            
            if (level.empty()) {
                it = ask_levels_.erase(it);
                update_best_ask();
            } else {
                ++it;
            }
        }
    } else {
        // Market sell matches against bids
        auto it = bid_levels_.begin();
        while (it != bid_levels_.end() && order->remaining_quantity() > 0) {
            PriceLevel& level = it->second;
            
            // Collect filled orders for cleanup
            std::vector<OrderId> filled_orders;
            
            Quantity matched = level.match_orders(
                order,
                order->remaining_quantity(),
                [this, now, &filled_orders](OrderId agg_id, OrderId pass_id, Price price, Quantity qty, Timestamp ts) {
                    this->notify_trade(agg_id, pass_id, price, qty, now);
                    
                    // Check passive order state after fill
                    Order* passive_order = order_pool_.find_order(pass_id);
                    if (passive_order) {
                        if (passive_order->is_filled()) {
                            filled_orders.push_back(pass_id);
                        } else if (passive_order->filled_quantity() > 0) {
                            // Passive order partially filled - notify
                            notify_order_event(pass_id, MX_EVENT_ORDER_PARTIAL,
                                             passive_order->filled_quantity(),
                                             passive_order->remaining_quantity());
                        }
                    }
                },
                now
            );
            
            result.matched_quantity += matched;
            total_trades_++;
            total_volume_ += matched;
            
            // Destroy fully filled passive orders (already removed from level by match_orders)
            for (OrderId filled_id : filled_orders) {
                Order* filled_order = order_pool_.find_order(filled_id);
                if (filled_order) {
                    notify_order_event(filled_id, MX_EVENT_ORDER_FILLED,
                                     filled_order->filled_quantity(), 0);
                    order_pool_.destroy_order(filled_order);
                }
            }
            
            if (level.empty()) {
                it = bid_levels_.erase(it);
                update_best_bid();
            } else {
                ++it;
            }
        }
    }
    
    result.remaining_quantity = order->remaining_quantity();
    result.fully_matched = (result.remaining_quantity == 0);
    
    return result;
}

/* ============================================================================
 * Special Order Type Handling
 * ========================================================================= */

MatchResult OrderBook::handle_ioc_order(Order* order) {
    // IOC: Match immediately, cancel remainder
    MatchResult result = match_order(order);
    
    if (order->remaining_quantity() > 0) {
        order->cancel();
        notify_order_event(order->order_id(), MX_EVENT_ORDER_CANCELLED,
                         order->filled_quantity(), 0);
    } else {
        notify_order_event(order->order_id(), MX_EVENT_ORDER_FILLED,
                         order->filled_quantity(), 0);
    }
    
    order_pool_.destroy_order(order);
    return result;
}

MatchResult OrderBook::handle_fok_order(Order* order) {
    // FOK: All or nothing
    MatchResult result;
    
    Quantity available = 0;
    if (!can_fill_fok(order, available)) {
        // Cannot fill completely - reject
        order->reject();
        notify_order_event(order->order_id(), MX_EVENT_ORDER_REJECTED, 0, 0);
        order_pool_.destroy_order(order);
        
        result.status = MX_STATUS_CANNOT_FILL;
        result.remaining_quantity = order->total_quantity();
        return result;
    }
    
    // Can fill - execute immediately
    result = match_order(order);
    
    if (order->is_filled()) {
        notify_order_event(order->order_id(), MX_EVENT_ORDER_FILLED,
                         order->filled_quantity(), 0);
    }
    
    order_pool_.destroy_order(order);
    return result;
}

mx_status_t OrderBook::handle_stop_order(Order* order) {
    // Check if already triggered
    if (should_trigger_stop(order)) {
        // Trigger immediately
        order->trigger_stop();
        return process_new_order(order);
    }
    
    // Add to stop orders map
    stop_orders_[order->order_id()] = order;
    
    notify_order_event(order->order_id(), MX_EVENT_ORDER_ACCEPTED, 0, 
                      order->remaining_quantity());
    
    return MX_STATUS_OK;
}

bool OrderBook::should_trigger_stop(const Order* stop_order) const {
    if (!stop_order->is_stop()) return false;
    
    Price trigger_price = stop_order->stop_price();
    
    if (stop_order->is_buy()) {
        // Buy stop triggers when market >= stop price
        return best_ask_ > 0 && best_ask_ >= trigger_price;
    } else {
        // Sell stop triggers when market <= stop price
        return best_bid_ > 0 && best_bid_ <= trigger_price;
    }
}

/* ============================================================================
 * Book Management
 * ========================================================================= */

void OrderBook::add_to_book(Order* order) {
    MX_ASSERT(order != nullptr);
    MX_ASSERT(!order->is_market()); // Market orders don't go in book
    
    PriceLevel* level = get_or_create_level(order->side(), order->price());
    level->add_order(order);
    
    // Update best prices
    if (order->is_buy()) {
        if (order->price() > best_bid_) {
            best_bid_ = order->price();
        }
    } else {
        if (best_ask_ == 0 || order->price() < best_ask_) {
            best_ask_ = order->price();
        }
    }
}

void OrderBook::remove_from_book(Order* order) {
    MX_ASSERT(order != nullptr);
    
    if (!order->is_active() && !order->is_partially_filled()) {
        return; // Not in book
    }
    
    PriceLevel* level = get_level(order->side(), order->price());
    if (level) {
        level->remove_order(order);
        
        // Remove level if empty
        remove_level_if_empty(order->side(), order->price());
        
        // Update best prices if needed
        if (order->is_buy() && order->price() == best_bid_) {
            update_best_bid();
        } else if (order->is_sell() && order->price() == best_ask_) {
            update_best_ask();
        }
    }
}

/* ============================================================================
 * Price Level Management
 * ========================================================================= */

PriceLevel* OrderBook::get_or_create_level(Side side, Price price) {
    if (side == MX_SIDE_BUY) {
        auto it = bid_levels_.find(price);
        if (it != bid_levels_.end()) {
            return &it->second;
        }
        
        // Create new level
        auto result = bid_levels_.emplace(price, PriceLevel(price));
        return &result.first->second;
    } else {
        auto it = ask_levels_.find(price);
        if (it != ask_levels_.end()) {
            return &it->second;
        }
        
        auto result = ask_levels_.emplace(price, PriceLevel(price));
        return &result.first->second;
    }
}

PriceLevel* OrderBook::get_level(Side side, Price price) {
    if (side == MX_SIDE_BUY) {
        auto it = bid_levels_.find(price);
        return (it != bid_levels_.end()) ? &it->second : nullptr;
    } else {
        auto it = ask_levels_.find(price);
        return (it != ask_levels_.end()) ? &it->second : nullptr;
    }
}

const PriceLevel* OrderBook::get_level(Side side, Price price) const {
    if (side == MX_SIDE_BUY) {
        auto it = bid_levels_.find(price);
        return (it != bid_levels_.end()) ? &it->second : nullptr;
    } else {
        auto it = ask_levels_.find(price);
        return (it != ask_levels_.end()) ? &it->second : nullptr;
    }
}

void OrderBook::remove_level_if_empty(Side side, Price price) {
    if (side == MX_SIDE_BUY) {
        auto it = bid_levels_.find(price);
        if (it != bid_levels_.end() && it->second.empty()) {
            bid_levels_.erase(it);
        }
    } else {
        auto it = ask_levels_.find(price);
        if (it != ask_levels_.end() && it->second.empty()) {
            ask_levels_.erase(it);
        }
    }
}

void OrderBook::update_best_bid() {
    if (bid_levels_.empty()) {
        best_bid_ = 0;
    } else {
        best_bid_ = bid_levels_.begin()->first; // Highest price (descending order)
    }
}

void OrderBook::update_best_ask() {
    if (ask_levels_.empty()) {
        best_ask_ = 0;
    } else {
        best_ask_ = ask_levels_.begin()->first; // Lowest price (ascending order)
    }
}

/* ============================================================================
 * Market Data Queries
 * ========================================================================= */

Quantity OrderBook::get_volume_at_price(Side side, Price price) const {
    const PriceLevel* level = get_level(side, price);
    return level ? level->total_volume() : 0;
}

uint64_t OrderBook::get_depth(Side side, uint32_t num_levels) const {
    uint64_t total_volume = 0;
    uint32_t count = 0;
    
    if (side == MX_SIDE_BUY) {
        for (const auto& pair : bid_levels_) {
            if (count >= num_levels) break;
            total_volume += pair.second.total_volume();
            ++count;
        }
    } else {
        for (const auto& pair : ask_levels_) {
            if (count >= num_levels) break;
            total_volume += pair.second.total_volume();
            ++count;
        }
    }
    
    return total_volume;
}

OrderBookStats OrderBook::get_stats() const {
    OrderBookStats stats;
    stats.total_orders = get_total_order_count();
    stats.bid_levels = get_bid_level_count();
    stats.ask_levels = get_ask_level_count();
    stats.best_bid = best_bid_;
    stats.best_ask = best_ask_;
    
    for (const auto& pair : bid_levels_) {
        stats.total_bid_volume += pair.second.total_volume();
    }
    
    for (const auto& pair : ask_levels_) {
        stats.total_ask_volume += pair.second.total_volume();
    }
    
    return stats;
}

/* ============================================================================
 * Administrative
 * ========================================================================= */

void OrderBook::clear() {
    bid_levels_.clear();
    ask_levels_.clear();
    stop_orders_.clear();
    order_pool_.clear();
    
    best_bid_ = 0;
    best_ask_ = 0;
}

uint32_t OrderBook::process_expirations(Timestamp current_time) {
    std::vector<OrderId> expired_orders;
    
    // Find expired orders
    order_pool_.for_each_order([current_time, &expired_orders](Order* order) {
        if (order->is_expired(current_time)) {
            expired_orders.push_back(order->order_id());
        }
    });
    
    // Cancel them
    for (OrderId id : expired_orders) {
        Order* order = order_pool_.find_order(id);
        if (order) {
            remove_from_book(order);
            order->set_state(OrderState::EXPIRED);
            notify_order_event(id, MX_EVENT_ORDER_EXPIRED, 
                             order->filled_quantity(), 0);
            order_pool_.destroy_order(order);
        }
    }
    
    return static_cast<uint32_t>(expired_orders.size());
}

uint32_t OrderBook::process_stops() {
    uint32_t triggered_count = 0;
    
    std::vector<OrderId> to_trigger;
    
    // Find stops that should trigger
    for (auto& pair : stop_orders_) {
        Order* stop_order = pair.second;
        if (should_trigger_stop(stop_order)) {
            to_trigger.push_back(stop_order->order_id());
        }
    }
    
    // Trigger them
    for (OrderId id : to_trigger) {
        auto it = stop_orders_.find(id);
        if (it != stop_orders_.end()) {
            Order* order = it->second;
            stop_orders_.erase(it);
            
            order->trigger_stop();
            notify_order_event(id, MX_EVENT_ORDER_TRIGGERED, 0, order->remaining_quantity());
            
            process_new_order(order);
            ++triggered_count;
        }
    }
    
    return triggered_count;
}

/* ============================================================================
 * Validation and Helpers
 * ========================================================================= */

bool OrderBook::would_match_immediately(const Order* order) const {
    if (order->is_buy()) {
        return best_ask_ > 0 && order->price() >= best_ask_;
    } else {
        return best_bid_ > 0 && order->price() <= best_bid_;
    }
}

bool OrderBook::can_fill_fok(const Order* order, Quantity& available_quantity) const {
    available_quantity = 0;
    
    if (order->is_buy()) {
        // Buy order checks against asks
        for (const auto& pair : ask_levels_) {
            const PriceLevel& level = pair.second;
            
            // Check price compatibility
            if (order->price() < level.price()) {
                break; // No more matchable prices
            }
            
            available_quantity += level.total_volume();
            
            if (available_quantity >= order->remaining_quantity()) {
                return true; // Can fill completely
            }
        }
    } else {
        // Sell order checks against bids
        for (const auto& pair : bid_levels_) {
            const PriceLevel& level = pair.second;
            
            // Check price compatibility
            if (order->price() > level.price()) {
                break; // No more matchable prices
            }
            
            available_quantity += level.total_volume();
            
            if (available_quantity >= order->remaining_quantity()) {
                return true; // Can fill completely
            }
        }
    }
    
    return false;
}

bool OrderBook::can_fill_aon(const Order* order) const {
    Quantity available = 0;
    return can_fill_fok(order, available);
}

mx_status_t OrderBook::validate_order(OrderId order_id, OrderType type, Side side,
                                      Price price, Price stop_price, Quantity quantity,
                                      TimeInForce tif, uint32_t flags) const {
    
    if (order_id == INVALID_ORDER_ID) return MX_STATUS_INVALID_PARAM;
    if (quantity == 0) return MX_STATUS_INVALID_QUANTITY;
    
    // Limit orders must have price
    if (type == MX_ORDER_TYPE_LIMIT || type == MX_ORDER_TYPE_STOP_LIMIT) {
        if (price == 0) return MX_STATUS_INVALID_PRICE;
    }
    
    // Stop orders must have stop price
    if (type == MX_ORDER_TYPE_STOP || type == MX_ORDER_TYPE_STOP_LIMIT) {
        if (stop_price == 0) return MX_STATUS_INVALID_PRICE;
    }
    
    // Check for duplicate
    if (order_pool_.has_order(order_id)) {
        return MX_STATUS_DUPLICATE_ORDER;
    }
    
    return MX_STATUS_OK;
}

/* ============================================================================
 * Callbacks
 * ========================================================================= */

void OrderBook::notify_trade(OrderId aggressive_id, OrderId passive_id,
                             Price price, Quantity quantity, Timestamp timestamp) {
    context_->callbacks().on_trade(aggressive_id, passive_id, price, quantity, timestamp);
}

void OrderBook::notify_order_event(OrderId order_id, mx_order_event_t event,
                                   Quantity filled, Quantity remaining) {
    context_->callbacks().on_order_event(order_id, event, filled, remaining);
}

Timestamp OrderBook::get_current_timestamp() const {
    return context_->get_timestamp();
}

} // namespace matchx
