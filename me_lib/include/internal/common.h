/**
 * Internal common definitions and macros
 * DO NOT include this from public headers
 */

#ifndef MX_INTERNAL_COMMON_H
#define MX_INTERNAL_COMMON_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cassert>

/* Include public API for types */
#include "matchengine.h"

namespace matchx {

/* ============================================================================
 * Compiler and Platform Detection
 * ========================================================================= */

#if defined(_MSC_VER)
    #define MX_FORCE_INLINE __forceinline
    #define MX_LIKELY(x) (x)
    #define MX_UNLIKELY(x) (x)
#elif defined(__GNUC__) || defined(__clang__)
    #define MX_FORCE_INLINE inline __attribute__((always_inline))
    #define MX_LIKELY(x) __builtin_expect(!!(x), 1)
    #define MX_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define MX_FORCE_INLINE inline
    #define MX_LIKELY(x) (x)
    #define MX_UNLIKELY(x) (x)
#endif

/* Cache line size for alignment */
#define MX_CACHE_LINE_SIZE 64

/* Align to cache line to prevent false sharing */
#define MX_CACHE_ALIGNED alignas(MX_CACHE_LINE_SIZE)

/* ============================================================================
 * Custom Allocator Macro for Classes
 * Following the article's pattern - place in private section of classes
 * ========================================================================= */

#define MX_IMPLEMENTS_ALLOCATORS \
public: \
    void* operator new(size_t size) { return mx_malloc(size); } \
    void operator delete(void* ptr) { mx_free(ptr); } \
    void* operator new[](size_t size) { return mx_malloc(size); } \
    void operator delete[](void* ptr) { mx_free(ptr); } \
    void* operator new(size_t, void* ptr) { return ptr; } \
    void operator delete(void*, void*) {} \
    void* operator new[](size_t, void* ptr) { return ptr; } \
    void operator delete[](void*, void*) {} \
private:

/* ============================================================================
 * Allocator Function Declarations
 * Implemented in allocator.cpp
 * ========================================================================= */

void* mx_malloc(size_t size);
void* mx_realloc(void* ptr, size_t size);
void* mx_calloc(size_t count, size_t size);
void mx_free(void* ptr);
char* mx_strdup(const char* str);

/* ============================================================================
 * Utility Macros
 * ========================================================================= */

#define MX_UNUSED(x) (void)(x)

/* Safe min/max without double evaluation */
#define MX_MIN(a, b) ((a) < (b) ? (a) : (b))
#define MX_MAX(a, b) ((a) > (b) ? (a) : (b))

/* Bit manipulation */
#define MX_SET_BIT(flags, bit) ((flags) |= (bit))
#define MX_CLEAR_BIT(flags, bit) ((flags) &= ~(bit))
#define MX_HAS_BIT(flags, bit) (((flags) & (bit)) != 0)

/* ============================================================================
 * Type Aliases for Convenience
 * ========================================================================= */

using OrderId = uint64_t;
using Price = uint32_t;
using Quantity = uint32_t;
using Timestamp = uint64_t;
using Side = mx_side_t;
using OrderType = mx_order_type_t;
using TimeInForce = mx_time_in_force_t;

/* Invalid/sentinel values */
constexpr Price INVALID_PRICE = 0;
constexpr Quantity INVALID_QUANTITY = 0;
constexpr OrderId INVALID_ORDER_ID = 0;

/* ============================================================================
 * Debug Macros
 * ========================================================================= */

#ifdef MX_DEBUG
    #define MX_ASSERT(expr) assert(expr)
    #define MX_DEBUG_PRINT(fmt, ...) fprintf(stderr, fmt "\n", ##__VA_ARGS__)
#else
    #define MX_ASSERT(expr) ((void)0)
    #define MX_DEBUG_PRINT(fmt, ...) ((void)0)
#endif

/* ============================================================================
 * Performance Hints
 * ========================================================================= */

/* Hint that this branch is rarely taken */
#define MX_COLD_PATH MX_UNLIKELY

/* Hint that this branch is commonly taken */
#define MX_HOT_PATH MX_LIKELY

} // namespace matchx

#endif // MX_INTERNAL_COMMON_H
