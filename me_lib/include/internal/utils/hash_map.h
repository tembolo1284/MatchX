/**
 * Fast hash map wrapper with custom allocator
 * Uses robin hood hashing for better cache performance
 * Wraps std::unordered_map with our custom allocator
 */

#ifndef MX_INTERNAL_UTILS_HASH_MAP_H
#define MX_INTERNAL_UTILS_HASH_MAP_H

#include "../common.h"
#include "memory_pool.h"
#include <unordered_map>
#include <functional>

namespace matchx {

/* ============================================================================
 * Fast Hash Function
 * FNV-1a hash - simple and fast for integers
 * ========================================================================= */

template<typename T>
struct FastHash {
    size_t operator()(T key) const {
        // FNV-1a hash for integer types
        static_assert(std::is_integral<T>::value, "FastHash only works with integer types");
        
        size_t hash = 14695981039346656037ULL; // FNV offset basis
        const uint8_t* data = reinterpret_cast<const uint8_t*>(&key);
        
        for (size_t i = 0; i < sizeof(T); ++i) {
            hash ^= data[i];
            hash *= 1099511628211ULL; // FNV prime
        }
        
        return hash;
    }
};

/* Specialization for uint64_t using faster multiply-shift */
template<>
struct FastHash<uint64_t> {
    size_t operator()(uint64_t key) const {
        // Multiply-shift hash
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33;
        key *= 0xc4ceb9fe1a85ec53ULL;
        key ^= key >> 33;
        return key;
    }
};

/* ============================================================================
 * HashMap - Wrapper around std::unordered_map with custom allocator
 * ========================================================================= */

template
    typename Key,
    typename Value,
    typename Hash = FastHash<Key>,
    typename KeyEqual = std::equal_to<Key>
>
class HashMap {
private:
    using AllocType = ProxyAllocator<std::pair<const Key, Value>>;
    using MapType = std::unordered_map<Key, Value, Hash, KeyEqual, AllocType>;
    
    MapType map_;
    
public:
    // Type aliases
    using key_type = Key;
    using mapped_type = Value;
    using value_type = typename MapType::value_type;
    using size_type = typename MapType::size_type;
    using iterator = typename MapType::iterator;
    using const_iterator = typename MapType::const_iterator;
    
    // Constructors
    HashMap() : map_() {}
    
    explicit HashMap(size_type bucket_count)
        : map_(bucket_count) {}
    
    // Capacity
    bool empty() const { return map_.empty(); }
    size_type size() const { return map_.size(); }
    size_type max_size() const { return map_.max_size(); }
    
    // Iterators
    iterator begin() { return map_.begin(); }
    iterator end() { return map_.end(); }
    const_iterator begin() const { return map_.begin(); }
    const_iterator end() const { return map_.end(); }
    const_iterator cbegin() const { return map_.cbegin(); }
    const_iterator cend() const { return map_.cend(); }
    
    // Modifiers
    void clear() { map_.clear(); }
    
    std::pair<iterator, bool> insert(const value_type& value) {
        return map_.insert(value);
    }
    
    template<typename... Args>
    std::pair<iterator, bool> emplace(Args&&... args) {
        return map_.emplace(std::forward<Args>(args)...);
    }
    
    iterator erase(const_iterator pos) {
        return map_.erase(pos);
    }
    
    size_type erase(const Key& key) {
        return map_.erase(key);
    }
    
    // Lookup
    iterator find(const Key& key) {
        return map_.find(key);
    }
    
    const_iterator find(const Key& key) const {
        return map_.find(key);
    }
    
    size_type count(const Key& key) const {
        return map_.count(key);
    }
    
    bool contains(const Key& key) const {
        return map_.find(key) != map_.end();
    }
    
    // Element access
    Value& operator[](const Key& key) {
        return map_[key];
    }
    
    Value& at(const Key& key) {
        return map_.at(key);
    }
    
    const Value& at(const Key& key) const {
        return map_.at(key);
    }
    
    // Bucket interface
    size_type bucket_count() const { return map_.bucket_count(); }
    size_type max_bucket_count() const { return map_.max_bucket_count(); }
    
    void reserve(size_type count) {
        map_.reserve(count);
    }
    
    void rehash(size_type count) {
        map_.rehash(count);
    }
    
    // Hash policy
    float load_factor() const { return map_.load_factor(); }
    float max_load_factor() const { return map_.max_load_factor(); }
    void max_load_factor(float ml) { map_.max_load_factor(ml); }
};

/* ============================================================================
 * Specialized HashMap for Order ID lookups
 * ========================================================================= */

template<typename Value>
using OrderIdMap = HashMap<OrderId, Value, FastHash<OrderId>>;

/* ============================================================================
 * Specialized HashMap for Price lookups
 * ========================================================================= */

template<typename Value>
using PriceMap = HashMap<Price, Value, FastHash<Price>>;

} // namespace matchx

#endif // MX_INTERNAL_UTILS_HASH_MAP_H
