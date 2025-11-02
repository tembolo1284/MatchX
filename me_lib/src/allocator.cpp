/**
 * Custom allocator implementation
 * Allows users to override memory allocation functions
 */

#include "internal/allocator.h"
#include <cstdlib>

namespace matchx {

/* ============================================================================
 * Global Allocator State
 * ========================================================================= */

// Initialize with standard malloc/realloc/free
Allocators g_allocators = {
    std::malloc,
    std::realloc,
    std::free
};

/* ============================================================================
 * Allocator Management Functions
 * ========================================================================= */

void set_allocators(MallocFunc malloc_fn, ReallocFunc realloc_fn, FreeFunc free_fn) {
    if (malloc_fn && realloc_fn && free_fn) {
        g_allocators.malloc_fn = malloc_fn;
        g_allocators.realloc_fn = realloc_fn;
        g_allocators.free_fn = free_fn;
    }
}

void reset_allocators() {
    g_allocators.malloc_fn = std::malloc;
    g_allocators.realloc_fn = std::realloc;
    g_allocators.free_fn = std::free;
}

} // namespace matchx

/* ============================================================================
 * C API Implementation
 * ========================================================================= */

extern "C" {

void mx_set_allocators(void* (*f_malloc)(size_t),
                       void* (*f_realloc)(void*, size_t),
                       void (*f_free)(void*)) {
    matchx::set_allocators(f_malloc, f_realloc, f_free);
}

} // extern "C"
