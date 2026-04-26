#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace hft {

// ─── ObjectPool<T, Capacity> ─────────────────────────────────────────────────
//
// A fixed-capacity, O(1) allocate/free pool allocator.
//
// WHY NOT malloc/new on the hot path?
//   malloc in the worst case:
//     - acquires a mutex (thread contention)
//     - calls sbrk/mmap (syscall = hundreds of nanoseconds)
//     - touches cold memory (cache miss)
//   In our Week 3 benchmark, the p99 spike (239ns vs 66ns p50) is largely
//   caused by these allocator jitters.
//
// HOW IT WORKS:
//   Pre-allocate a flat array of T objects at construction time.
//   Maintain a free-list of available slots as a stack of indices.
//
//   allocate() → pop index from free stack, return pointer to slot  O(1)
//   free(ptr)  → push index back onto free stack                    O(1)
//
//   No mutex needed if the pool is used from a single thread
//   (which is the case for our single-threaded matching engine).
//
// MEMORY LAYOUT:
//   storage_[]  — raw aligned storage, one slot per T
//   free_stack_ — indices of available slots (grows downward)
//   top_        — current top of free stack
//
//   All storage is allocated once at construction (on the stack if the
//   pool itself is stack-allocated, or on the heap if heap-allocated).
//   After construction, zero dynamic allocation occurs.
//
template<typename T, size_t Capacity>
class ObjectPool {
public:
    ObjectPool() {
        // Initialize free stack with all indices available.
        // Stack grows from Capacity downward; top_ points to next free slot.
        for (size_t i = 0; i < Capacity; ++i) {
            free_stack_[i] = static_cast<uint32_t>(Capacity - 1 - i);
        }
        top_ = Capacity;
    }

    // Non-copyable, non-movable — pointers into pool storage must stay valid.
    ObjectPool(const ObjectPool&)            = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&)                 = delete;
    ObjectPool& operator=(ObjectPool&&)      = delete;

    // ── allocate ──────────────────────────────────────────────
    //
    // Returns a pointer to uninitialized storage for one T.
    // Caller must placement-new into it:
    //   T* p = pool.allocate();
    //   new (p) T(...);
    //
    // Returns nullptr if pool is exhausted.
    //
    T* allocate() noexcept {
        if (top_ == 0) [[unlikely]] return nullptr;
        uint32_t idx = free_stack_[--top_];
        return reinterpret_cast<T*>(&storage_[idx]);
    }

    // ── free ──────────────────────────────────────────────────
    //
    // Return a previously allocated slot back to the pool.
    // Does NOT call the destructor — caller is responsible.
    //
    void free(T* ptr) noexcept {
        assert(ptr != nullptr);
        assert(owns(ptr));
        uint32_t idx = static_cast<uint32_t>(
            reinterpret_cast<Slot*>(ptr) - &storage_[0]);
        assert(top_ < Capacity);
        free_stack_[top_++] = idx;
    }

    // ── make ──────────────────────────────────────────────────
    //
    // Convenience: allocate + placement-new in one call.
    // Usage: Order* o = pool.make<Order>(Order::make_limit(...));
    //
    template<typename... Args>
    T* make(Args&&... args) noexcept {
        T* slot = allocate();
        if (slot == nullptr) [[unlikely]] return nullptr;
        return new (slot) T(std::forward<Args>(args)...);
    }

    // ── destroy ───────────────────────────────────────────────
    //
    // Call destructor + return to pool.
    //
    void destroy(T* ptr) noexcept {
        ptr->~T();
        free(ptr);
    }

    // ── Accessors ─────────────────────────────────────────────
    size_t capacity()  const noexcept { return Capacity; }
    size_t available() const noexcept { return top_; }
    size_t in_use()    const noexcept { return Capacity - top_; }
    bool   empty()     const noexcept { return top_ == Capacity; }
    bool   full()      const noexcept { return top_ == 0; }

    // Returns true if ptr points into this pool's storage.
    bool owns(const T* ptr) const noexcept {
        const auto* p = reinterpret_cast<const Slot*>(ptr);
        return p >= &storage_[0] && p < &storage_[Capacity];
    }

private:
    // Raw storage aligned to T's alignment requirement.
    // Using a union-like aligned_storage to avoid constructing T objects
    // until allocate() is called.
    using Slot = std::aligned_storage_t<sizeof(T), alignof(T)>;

    Slot     storage_[Capacity];
    uint32_t free_stack_[Capacity];
    size_t   top_;
};

} // namespace hft
