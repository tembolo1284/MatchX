/**
 * Fast hash map wrapper with custom allocator
 */

#ifndef MX_INTERNAL_UTILS_HASH_MAP_H
#define MX_INTERNAL_UTILS_HASH_MAP_H

#include "../common.h"
#include "memory_pool.h"
#include <unordered_map>
#include <functional>

namespace matchx {

/* Fast Hash Function */
template<typename T>
struct FastHash {
    size_t operator()(T key) const {
        static_assert(std::is_integral<T>::value, "FastHash only works with integer types");
        
        size_t hash = 14695981039346656037ULL;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(&key);
        
        for (size_t i = 0; i < sizeof(T); ++i) {
            hash ^= data[i];
            hash *= 1099511628211ULL;
        }
        
        return hash;
    }
};

template<>
struct FastHash<uint64_t> {
    size_t operator()(uint64_t key) const {
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdULL;
        key ^= key >> 33;
        key *= 0xc4ceb9fe1a85ec53ULL;
        key ^= key >> 33;
        return key;
    }
};

/* HashMap wrapper */
template<typename Key, typename Value, typename Hash = FastHash<Key>, typename KeyEqual = std::equal_to<Key> >
class HashMap {
private:
    typedef ProxyAllocator<std::pair<const Key, Value> > AllocType;
    typedef std::unordered_map<Key, Value, Hash, KeyEqual, AllocType> MapType;
    
    MapType map_;
    
public:
    typedef Key key_type;
    typedef Value mapped_type;
    typedef typename MapType::value_type value_type;
    typedef typename MapType::size_type size_type;
    typedef typename MapType::iterator iterator;
    typedef typename MapType::const_iterator const_iterator;
    
    HashMap() : map_() {}
    
    explicit HashMap(size_type bucket_count) : map_(bucket_count) {}
    
    bool empty() const { return map_.empty(); }
    size_type size() const { return map_.size(); }
    size_type max_size() const { return map_.max_size(); }
    
    iterator begin() { return map_.begin(); }
    iterator end() { return map_.end(); }
    const_iterator begin() const { return map_.begin(); }
    const_iterator end() const { return map_.end(); }
    const_iterator cbegin() const { return map_.cbegin(); }
    const_iterator cend() const { return map_.cend(); }
    
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
    
    Value& operator[](const Key& key) {
        return map_[key];
    }
    
    Value& at(const Key& key) {
        return map_.at(key);
    }
    
    const Value& at(const Key& key) const {
        return map_.at(key);
    }
    
    size_type bucket_count() const { return map_.bucket_count(); }
    size_type max_bucket_count() const { return map_.max_bucket_count(); }
    
    void reserve(size_type count) {
        map_.reserve(count);
    }
    
    void rehash(size_type count) {
        map_.rehash(count);
    }
    
    float load_factor() const { return map_.load_factor(); }
    float max_load_factor() const { return map_.max_load_factor(); }
    void max_load_factor(float ml) { map_.max_load_factor(ml); }
};

/* Specialized typedefs */
template<typename Value>
class OrderIdMap : public HashMap<OrderId, Value, FastHash<OrderId> > {
public:
    OrderIdMap() : HashMap<OrderId, Value, FastHash<OrderId> >() {}
    explicit OrderIdMap(typename HashMap<OrderId, Value, FastHash<OrderId> >::size_type n) 
        : HashMap<OrderId, Value, FastHash<OrderId> >(n) {}
};

template<typename Value>
class PriceMap : public HashMap<Price, Value, FastHash<Price> > {
public:
    PriceMap() : HashMap<Price, Value, FastHash<Price> >() {}
    explicit PriceMap(typename HashMap<Price, Value, FastHash<Price> >::size_type n)
        : HashMap<Price, Value, FastHash<Price> >(n) {}
};

} // namespace matchx

#endif // MX_INTERNAL_UTILS_HASH_MAP_H
