/**
 * Memory pool for fast object allocation
 * Pre-allocates a block of memory and dispenses objects from it
 * Used for Order objects to avoid malloc/free in hot path
 */

#ifndef MX_INTERNAL_UTILS_MEMORY_POOL_H
#define MX_INTERNAL_UTILS_MEMORY_POOL_H

#include "../common.h"
#include <vector>

namespace matchx {

/* ============================================================================
 * Memory Pool
 * Fixed-size object pool with freelist
 * ========================================================================= */

template<typename T>
class MemoryPool {
private:
    // Pool configuration
    size_t chunk_size_;           // Objects per chunk
    size_t total_capacity_;       // Total objects allocated
    size_t allocated_count_;      // Objects currently in use
    
    // Storage
    std::vector<T*> chunks_;      // Array of memory chunks
    T* free_list_head_;           // Head of free list
    
    // Union for free list linkage
    union FreeNode {
        T object;
        FreeNode* next;
    };
    
    MX_IMPLEMENTS_ALLOCATORS

public:
    explicit MemoryPool(size_t initial_chunk_size = 1024)
        : chunk_size_(initial_chunk_size)
        , total_capacity_(0)
        , allocated_count_(0)
        , free_list_head_(nullptr) {
        
        // Allocate initial chunk
        allocate_chunk();
    }
    
    ~MemoryPool() {
        // Free all chunks
        for (T* chunk : chunks_) {
            mx_free(chunk);
        }
    }
    
    // Non-copyable
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;
    
    /* ========================================================================
     * Allocation
     * ===================================================================== */
    
    /**
     * Allocate an object from the pool
     * Returns raw memory, caller must call placement new
     */
    T* allocate() {
        if (MX_UNLIKELY(free_list_head_ == nullptr)) {
            allocate_chunk();
        }
        
        FreeNode* node = reinterpret_cast<FreeNode*>(free_list_head_);
        free_list_head_ = reinterpret_cast<T*>(node->next);
        
        ++allocated_count_;
        return &node->object;
    }
    
    /**
     * Deallocate an object back to the pool
     * Caller must call destructor before calling this
     */
    void deallocate(T* ptr) {
        if (!ptr) return;
        
        MX_ASSERT(allocated_count_ > 0);
        
        FreeNode* node = reinterpret_cast<FreeNode*>(ptr);
        node->next = reinterpret_cast<FreeNode*>(free_list_head_);
        free_list_head_ = ptr;
        
        --allocated_count_;
    }
    
    /**
     * Construct object in-place (combines allocate + placement new)
     */
    template<typename... Args>
    T* construct(Args&&... args) {
        T* ptr = allocate();
        new (ptr) T(std::forward<Args>(args)...);
        return ptr;
    }
    
    /**
     * Destroy object (calls destructor + deallocate)
     */
    void destroy(T* ptr) {
        if (!ptr) return;
        ptr->~T();
        deallocate(ptr);
    }
    
    /* ========================================================================
     * Statistics
     * ===================================================================== */
    
    size_t capacity() const { return total_capacity_; }
    size_t allocated() const { return allocated_count_; }
    size_t available() const { return total_capacity_ - allocated_count_; }
    size_t chunk_count() const { return chunks_.size(); }
    
    bool empty() const { return allocated_count_ == 0; }
    
    /**
     * Get memory usage in bytes
     */
    size_t memory_usage() const {
        return total_capacity_ * sizeof(T);
    }
    
    /* ========================================================================
     * Management
     * ===================================================================== */
    
    /**
     * Pre-allocate capacity for expected number of objects
     */
    void reserve(size_t count) {
        while (total_capacity_ < count) {
            allocate_chunk();
        }
    }
    
    /**
     * Clear all allocations (does NOT call destructors!)
     * Use only if you know all objects have been properly destroyed
     */
    void reset() {
        free_list_head_ = nullptr;
        allocated_count_ = 0;
        
        // Rebuild free list from all chunks
        for (T* chunk : chunks_) {
            FreeNode* chunk_nodes = reinterpret_cast<FreeNode*>(chunk);
            for (size_t i = 0; i < chunk_size_; ++i) {
                chunk_nodes[i].next = reinterpret_cast<FreeNode*>(free_list_head_);
                free_list_head_ = reinterpret_cast<T*>(&chunk_nodes[i]);
            }
        }
    }

private:
    /**
     * Allocate a new chunk and add to free list
     */
    void allocate_chunk() {
        // Allocate memory for chunk_size_ objects
        T* chunk = static_cast<T*>(mx_malloc(chunk_size_ * sizeof(T)));
        if (!chunk) {
            // Out of memory - this is fatal in our design
            return;
        }
        
        chunks_.push_back(chunk);
        total_capacity_ += chunk_size_;
        
        // Add all objects in this chunk to free list
        FreeNode* nodes = reinterpret_cast<FreeNode*>(chunk);
        for (size_t i = 0; i < chunk_size_; ++i) {
            nodes[i].next = reinterpret_cast<FreeNode*>(free_list_head_);
            free_list_head_ = reinterpret_cast<T*>(&nodes[i]);
        }
    }
};

/* ============================================================================
 * STL-Compatible Allocator Wrapper for Containers
 * Following the article's proxy_allocator pattern
 * ========================================================================= */

template<typename T>
class ProxyAllocator {
public:
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    typedef T* pointer;
    typedef const T* const_pointer;
    typedef T& reference;
    typedef const T& const_reference;
    typedef T value_type;
    
    template<typename U>
    struct rebind {
        typedef ProxyAllocator<U> other;
    };
    
    ProxyAllocator() throw() {}
    ProxyAllocator(const ProxyAllocator&) throw() {}
    
    template<typename U>
    ProxyAllocator(const ProxyAllocator<U>&) throw() {}
    
    ~ProxyAllocator() throw() {}
    
    pointer address(reference x) const { return &x; }
    const_pointer address(const_reference x) const { return &x; }
    
    pointer allocate(size_type n, const void* = 0) {
        if (n == 0) return nullptr;
        return static_cast<pointer>(mx_malloc(n * sizeof(T)));
    }
    
    void deallocate(pointer p, size_type) {
        mx_free(p);
    }
    
    size_type max_size() const throw() {
        return size_t(-1) / sizeof(T);
    }
    
    void construct(pointer p, const T& val) {
        new(static_cast<void*>(p)) T(val);
    }
    
    void destroy(pointer p) {
        p->~T();
    }
    
    bool operator==(const ProxyAllocator&) const { return true; }
    bool operator!=(const ProxyAllocator&) const { return false; }
};

/* ============================================================================
 * Type Aliases for STL Containers with Custom Allocator
 * ========================================================================= */

template<typename T>
using Vector = std::vector<T, ProxyAllocator<T>>;

} // namespace matchx

#endif // MX_INTERNAL_UTILS_MEMORY_POOL_H
