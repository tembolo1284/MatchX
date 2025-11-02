/**
 * Custom memory allocator interface
 * Allows users to control all memory allocations
 */

#ifndef MX_INTERNAL_ALLOCATOR_H
#define MX_INTERNAL_ALLOCATOR_H

#include "common.h"

namespace matchx {

/* ============================================================================
 * Allocator Function Pointer Types
 * ========================================================================= */

typedef void* (*MallocFunc)(size_t);
typedef void* (*ReallocFunc)(void*, size_t);
typedef void (*FreeFunc)(void*);

/* ============================================================================
 * Global Allocator Structure
 * ========================================================================= */

struct Allocators {
    MallocFunc malloc_fn;
    ReallocFunc realloc_fn;
    FreeFunc free_fn;
};

/* Global allocators - defined in allocator.cpp */
extern Allocators g_allocators;

/* ============================================================================
 * Allocator Initialization
 * ========================================================================= */

/**
 * Set custom allocators (called from mx_set_allocators C API)
 */
void set_allocators(MallocFunc malloc_fn, ReallocFunc realloc_fn, FreeFunc free_fn);

/**
 * Reset allocators to default (malloc/realloc/free)
 */
void reset_allocators();

/* ============================================================================
 * Inline Allocation Functions
 * These are the functions that all code should use
 * ========================================================================= */

inline void* mx_malloc(size_t size) {
    return g_allocators.malloc_fn(size);
}

inline void* mx_realloc(void* ptr, size_t size) {
    return g_allocators.realloc_fn(ptr, size);
}

inline void mx_free(void* ptr) {
    if (ptr) {
        g_allocators.free_fn(ptr);
    }
}

inline void* mx_calloc(size_t count, size_t size) {
    size_t total = count * size;
    void* ptr = mx_malloc(total);
    if (ptr) {
        std::memset(ptr, 0, total);
    }
    return ptr;
}

inline char* mx_strdup(const char* str) {
    if (!str) return nullptr;
    size_t len = std::strlen(str) + 1;
    char* dup = static_cast<char*>(mx_malloc(len));
    if (dup) {
        std::memcpy(dup, str, len);
    }
    return dup;
}

} // namespace matchx

#endif // MX_INTERNAL_ALLOCATOR_H
