/**
 * Context implementation
 * Manages global state, callbacks, and configuration
 */

#include "internal/context.h"
#include "internal/allocator.h"

namespace matchx {

// Context implementation is mostly in the header (inline methods)
// This file contains any non-inline implementations if needed

} // namespace matchx

/* ============================================================================
 * C API Implementation
 * ========================================================================= */

extern "C" {

mx_context_t* mx_context_new(void) {
    matchx::Context* ctx = new matchx::Context();
    return reinterpret_cast<mx_context_t*>(ctx);
}

void mx_context_free(mx_context_t* ctx) {
    if (!ctx) return;
    
    matchx::Context* context = reinterpret_cast<matchx::Context*>(ctx);
    delete context;
}

void mx_context_set_callbacks(mx_context_t* ctx,
                              mx_trade_callback_t trade_cb,
                              mx_order_callback_t order_cb,
                              void* user_data) {
    if (!ctx) return;
    
    matchx::Context* context = reinterpret_cast<matchx::Context*>(ctx);
    context->set_callbacks(trade_cb, order_cb, user_data);
}

void mx_context_set_timestamp(mx_context_t* ctx, uint64_t timestamp) {
    if (!ctx) return;
    
    matchx::Context* context = reinterpret_cast<matchx::Context*>(ctx);
    context->set_timestamp(timestamp);
}

uint64_t mx_context_get_timestamp(const mx_context_t* ctx) {
    if (!ctx) return 0;
    
    const matchx::Context* context = reinterpret_cast<const matchx::Context*>(ctx);
    return context->get_timestamp();
}

} // extern "C"
