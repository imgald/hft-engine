#include "engine/order_book.hpp"
#include <stdexcept>

namespace hft {

void OrderBook::add_order(Order* order) {
    if (order->side == Side::Buy) {
        // emplace avoids a second lookup vs operator[]+construct
        auto [it, inserted] = bids_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(order->price),
            std::forward_as_tuple(order->price)
        );
        it->second.add(order);
    } else {
        auto [it, inserted] = asks_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(order->price),
            std::forward_as_tuple(order->price)
        );
        it->second.add(order);
    }
    order_location_.emplace(order->order_id, order->price);
}

bool OrderBook::cancel_order(OrderId id) {
    auto loc_it = order_location_.find(id);
    if (loc_it == order_location_.end()) return false;

    Price price = loc_it->second;
    order_location_.erase(loc_it);

    // Try bid side first (we don't know side from just the id).
    // In Week 2 we'll add side to the location map to avoid the second lookup.
    if (auto it = bids_.find(price); it != bids_.end()) {
        it->second.remove(id);
        if (it->second.empty()) bids_.erase(it);
        return true;
    }
    if (auto it = asks_.find(price); it != asks_.end()) {
        it->second.remove(id);
        if (it->second.empty()) asks_.erase(it);
        return true;
    }
    return false;   // should not happen
}

} // namespace hft
