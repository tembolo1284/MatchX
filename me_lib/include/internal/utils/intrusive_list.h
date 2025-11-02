/**
 * Intrusive doubly-linked list
 * Orders embed the next/prev pointers directly for zero-allocation operations
 */

#ifndef MX_INTERNAL_UTILS_INTRUSIVE_LIST_H
#define MX_INTERNAL_UTILS_INTRUSIVE_LIST_H

#include "../common.h"

namespace matchx {

/* ============================================================================
 * Intrusive List Node
 * Embed this in your class to make it list-compatible
 * ========================================================================= */

template<typename T>
struct IntrusiveListNode {
    T* next;
    T* prev;
    
    IntrusiveListNode() : next(nullptr), prev(nullptr) {}
    
    // Check if node is linked
    bool is_linked() const {
        return next != nullptr || prev != nullptr;
    }
    
    // Unlink this node
    void unlink() {
        next = nullptr;
        prev = nullptr;
    }
};

/* ============================================================================
 * Intrusive List
 * Maintains head/tail pointers, all operations are O(1)
 * ========================================================================= */

template<typename T>
class IntrusiveList {
private:
    T* head_;
    T* tail_;
    uint32_t size_;

public:
    IntrusiveList() : head_(nullptr), tail_(nullptr), size_(0) {}
    
    ~IntrusiveList() {
        // Note: Does NOT delete nodes - they are owned externally
        clear();
    }
    
    // Non-copyable
    IntrusiveList(const IntrusiveList&) = delete;
    IntrusiveList& operator=(const IntrusiveList&) = delete;
    
    /* ========================================================================
     * Accessors
     * ===================================================================== */
    
    T* head() const { return head_; }
    T* tail() const { return tail_; }
    uint32_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    
    /* ========================================================================
     * List Operations - All O(1)
     * ===================================================================== */
    
    /**
     * Push to back (FIFO - for time priority)
     */
    void push_back(T* node) {
        MX_ASSERT(node != nullptr);
        MX_ASSERT(!node->is_linked()); // Node must not already be in a list
        
        node->next = nullptr;
        node->prev = tail_;
        
        if (tail_) {
            tail_->next = node;
        } else {
            head_ = node; // List was empty
        }
        
        tail_ = node;
        ++size_;
    }
    
    /**
     * Push to front (for priority jumping)
     */
    void push_front(T* node) {
        MX_ASSERT(node != nullptr);
        MX_ASSERT(!node->is_linked());
        
        node->prev = nullptr;
        node->next = head_;
        
        if (head_) {
            head_->prev = node;
        } else {
            tail_ = node; // List was empty
        }
        
        head_ = node;
        ++size_;
    }
    
    /**
     * Pop from front (for matching)
     */
    T* pop_front() {
        if (!head_) return nullptr;
        
        T* node = head_;
        head_ = node->next;
        
        if (head_) {
            head_->prev = nullptr;
        } else {
            tail_ = nullptr; // List is now empty
        }
        
        node->unlink();
        --size_;
        return node;
    }
    
    /**
     * Pop from back
     */
    T* pop_back() {
        if (!tail_) return nullptr;
        
        T* node = tail_;
        tail_ = node->prev;
        
        if (tail_) {
            tail_->next = nullptr;
        } else {
            head_ = nullptr; // List is now empty
        }
        
        node->unlink();
        --size_;
        return node;
    }
    
    /**
     * Remove a specific node from anywhere in the list - O(1)!
     * This is the key advantage of intrusive lists
     */
    void remove(T* node) {
        MX_ASSERT(node != nullptr);
        
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            // Node was head
            head_ = node->next;
        }
        
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            // Node was tail
            tail_ = node->prev;
        }
        
        node->unlink();
        --size_;
    }
    
    /**
     * Insert after a specific node
     */
    void insert_after(T* existing, T* new_node) {
        MX_ASSERT(existing != nullptr);
        MX_ASSERT(new_node != nullptr);
        MX_ASSERT(!new_node->is_linked());
        
        new_node->prev = existing;
        new_node->next = existing->next;
        
        if (existing->next) {
            existing->next->prev = new_node;
        } else {
            tail_ = new_node; // Inserted at end
        }
        
        existing->next = new_node;
        ++size_;
    }
    
    /**
     * Insert before a specific node
     */
    void insert_before(T* existing, T* new_node) {
        MX_ASSERT(existing != nullptr);
        MX_ASSERT(new_node != nullptr);
        MX_ASSERT(!new_node->is_linked());
        
        new_node->next = existing;
        new_node->prev = existing->prev;
        
        if (existing->prev) {
            existing->prev->next = new_node;
        } else {
            head_ = new_node; // Inserted at beginning
        }
        
        existing->prev = new_node;
        ++size_;
    }
    
    /**
     * Clear the list (does not delete nodes)
     */
    void clear() {
        T* current = head_;
        while (current) {
            T* next = current->next;
            current->unlink();
            current = next;
        }
        head_ = tail_ = nullptr;
        size_ = 0;
    }
    
    /* ========================================================================
     * Iteration Support
     * ===================================================================== */
    
    /**
     * Iterator for range-based for loops
     */
    class Iterator {
    private:
        T* current_;
        
    public:
        explicit Iterator(T* node) : current_(node) {}
        
        T* operator*() const { return current_; }
        T* operator->() const { return current_; }
        
        Iterator& operator++() {
            if (current_) current_ = current_->next;
            return *this;
        }
        
        bool operator!=(const Iterator& other) const {
            return current_ != other.current_;
        }
        
        bool operator==(const Iterator& other) const {
            return current_ == other.current_;
        }
    };
    
    Iterator begin() const { return Iterator(head_); }
    Iterator end() const { return Iterator(nullptr); }
};

} // namespace matchx

#endif // MX_INTERNAL_UTILS_INTRUSIVE_LIST_H
