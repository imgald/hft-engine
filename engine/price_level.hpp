#pragma once

#include "engine/order.hpp"
#include <list>
#include <unordered_map>
#include <cassert>

namespace hft {

// ─── PriceLevel ──────────────────────────────────────────────────────────────
//
// Represents all resting orders at a single price point on one side of the book.
//
// Data structure choice:
//   • std::list<Order*> for the FIFO queue
//       - O(1) push_back (new orders go to back)
//       - O(1) pop_front (first order matched first — price-time priority)
//       - Iterators and pointers remain STABLE after insert/erase
//         (this is the critical property we exploit for O(1) cancel)
//
//   • std::unordered_map<OrderId, list::iterator> for O(1) cancel
//       - Storing the iterator lets us jump directly to any order and
//         erase it from the list in O(1), without scanning.
//       - This is the standard HFT trick for fast cancel.
//
// Why not std::deque or std::vector?
//   - std::deque: iterators invalidate on push_front; complex layout.
//   - std::vector: erase is O(n); iterators invalidate on push_back.
//   - std::list: only container guaranteeing stable iterators on both
//     insert and erase — exactly what we need.
//
class PriceLevel {
public:
    using Queue    = std::list<Order*>;
    using Iterator = Queue::iterator;

    explicit PriceLevel(Price price) noexcept
        : price_(price), total_qty_(0) {}

    // Non-copyable (owns iterator handles into its own list).
    PriceLevel(const PriceLevel&)            = delete;
    PriceLevel& operator=(const PriceLevel&) = delete;
    PriceLevel(PriceLevel&&)                 = default;
    PriceLevel& operator=(PriceLevel&&)      = default;

    // ── Mutators ──────────────────────────────────────────────

    // Add a new order to the back of the FIFO queue.  O(1).
    void add(Order* order) {
        assert(order != nullptr);
        assert(order->price == price_);
        auto it = queue_.insert(queue_.end(), order);
        index_.emplace(order->order_id, it);
        total_qty_ += order->remaining_qty;
    }

    // Remove an order by id (cancel).  O(1) amortized.
    // Returns true if found and removed.
    bool remove(OrderId id) {
        auto map_it = index_.find(id);
        if (map_it == index_.end()) return false;

        Order* order = *map_it->second;
        total_qty_  -= order->remaining_qty;
        queue_.erase(map_it->second);
        index_.erase(map_it);
        return true;
    }

    // Reduce the quantity of the front order (partial fill).  O(1).
    void reduce_front(Quantity filled_qty) {
        assert(!queue_.empty());
        Order* front = queue_.front();
        assert(filled_qty <= front->remaining_qty);
        front->remaining_qty -= filled_qty;
        total_qty_           -= filled_qty;

        if (front->remaining_qty == 0) {
            // Fully filled — remove from both queue and index.
            index_.erase(front->order_id);
            queue_.pop_front();
        }
    }

    // ── Accessors ─────────────────────────────────────────────

    Price    price()     const noexcept { return price_; }
    Quantity total_qty() const noexcept { return total_qty_; }
    bool     empty()     const noexcept { return queue_.empty(); }
    size_t   depth()     const noexcept { return queue_.size(); }

    // Peek at the first (oldest) order without removing it.
    Order* front() noexcept {
        assert(!queue_.empty());
        return queue_.front();
    }
    const Order* front() const noexcept {
        assert(!queue_.empty());
        return queue_.front();
    }

    // Check if an order exists at this level.
    bool contains(OrderId id) const noexcept {
        return index_.count(id) > 0;
    }

private:
    Price       price_;
    Quantity    total_qty_;
    Queue       queue_;
    std::unordered_map<OrderId, Iterator> index_;
};

} // namespace hft
