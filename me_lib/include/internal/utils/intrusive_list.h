/**
 * Intrusive doubly-linked list
 * Orders embed the next/prev pointers directly for zero-allocation operations
 */

#ifndef MX_INTERNAL_UTILS_INTRUSIVE_LIST_H
#define MX_INTERNAL_UTILS_INTRUSIVE_LIST_H

#include "../common.h"

namespace matchx {

/* Intrusive List Node */
template<typename T>
struct IntrusiveListNode {
    T* next;
    T* prev;
    
    IntrusiveListNode() : next(nullptr), prev(nullptr) {}
    
    bool is_linked() const {
        return next != nullptr || prev != nullptr;
    }
    
    void unlink() {
        next = nullptr;
        prev = nullptr;
    }
};

/* Intrusive List */
template<typename T>
class IntrusiveList {
private:
    T* head_;
    T* tail_;
    uint32_t size_;

public:
    IntrusiveList() : head_(nullptr), tail_(nullptr), size_(0) {}
    
    ~IntrusiveList() {
        clear();
    }
    
    // Non-copyable
    IntrusiveList(const IntrusiveList&) = delete;
    IntrusiveList& operator=(const IntrusiveList&) = delete;
    
    // Moveable
    IntrusiveList(IntrusiveList&& other) noexcept
        : head_(other.head_)
        , tail_(other.tail_)
        , size_(other.size_) {
        other.head_ = nullptr;
        other.tail_ = nullptr;
        other.size_ = 0;
    }
    
    IntrusiveList& operator=(IntrusiveList&& other) noexcept {
        if (this != &other) {
            clear();
            head_ = other.head_;
            tail_ = other.tail_;
            size_ = other.size_;
            other.head_ = nullptr;
            other.tail_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }
    
    /* Accessors */
    T* head() const { return head_; }
    T* tail() const { return tail_; }
    uint32_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    
    /* List Operations */
    void push_back(T* node) {
        MX_ASSERT(node != nullptr);
        MX_ASSERT(!node->is_linked());
        
        node->next = nullptr;
        node->prev = tail_;
        
        if (tail_) {
            tail_->next = node;
        } else {
            head_ = node;
        }
        
        tail_ = node;
        ++size_;
    }
    
    void push_front(T* node) {
        MX_ASSERT(node != nullptr);
        MX_ASSERT(!node->is_linked());
        
        node->prev = nullptr;
        node->next = head_;
        
        if (head_) {
            head_->prev = node;
        } else {
            tail_ = node;
        }
        
        head_ = node;
        ++size_;
    }
    
    T* pop_front() {
        if (!head_) return nullptr;
        
        T* node = head_;
        head_ = node->next;
        
        if (head_) {
            head_->prev = nullptr;
        } else {
            tail_ = nullptr;
        }
        
        node->unlink();
        --size_;
        return node;
    }
    
    T* pop_back() {
        if (!tail_) return nullptr;
        
        T* node = tail_;
        tail_ = node->prev;
        
        if (tail_) {
            tail_->next = nullptr;
        } else {
            head_ = nullptr;
        }
        
        node->unlink();
        --size_;
        return node;
    }
    
    void remove(T* node) {
        MX_ASSERT(node != nullptr);
        
        if (node->prev) {
            node->prev->next = node->next;
        } else {
            head_ = node->next;
        }
        
        if (node->next) {
            node->next->prev = node->prev;
        } else {
            tail_ = node->prev;
        }
        
        node->unlink();
        --size_;
    }
    
    void insert_after(T* existing, T* new_node) {
        MX_ASSERT(existing != nullptr);
        MX_ASSERT(new_node != nullptr);
        MX_ASSERT(!new_node->is_linked());
        
        new_node->prev = existing;
        new_node->next = existing->next;
        
        if (existing->next) {
            existing->next->prev = new_node;
        } else {
            tail_ = new_node;
        }
        
        existing->next = new_node;
        ++size_;
    }
    
    void insert_before(T* existing, T* new_node) {
        MX_ASSERT(existing != nullptr);
        MX_ASSERT(new_node != nullptr);
        MX_ASSERT(!new_node->is_linked());
        
        new_node->next = existing;
        new_node->prev = existing->prev;
        
        if (existing->prev) {
            existing->prev->next = new_node;
        } else {
            head_ = new_node;
        }
        
        existing->prev = new_node;
        ++size_;
    }
    
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
    
    /* Iterator */
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
