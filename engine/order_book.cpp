#include "engine/order_book.hpp"
#include <stdexcept>

namespace hft {

void OrderBook::add_order(Order* order) {
    if (order->side == Side::Buy) {
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
    return false;
}

std::pair<Quantity, OrderId> OrderBook::fill_front(Price price, Side side, Quantity qty) {
    PriceLevel* level = nullptr;
    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it == bids_.end()) return {0, 0};
        level = &it->second;
    } else {
        auto it = asks_.find(price);
        if (it == asks_.end()) return {0, 0};
        level = &it->second;
    }

    if (level->empty()) return {0, 0};

    Order*   passive    = level->front();
    OrderId  passive_id = passive->order_id;
    Quantity fill_qty   = std::min(qty, passive->remaining_qty);

    level->reduce_front(fill_qty);

    if (level->empty()) {
        order_location_.erase(passive_id);
        if (side == Side::Buy) bids_.erase(price);
        else                   asks_.erase(price);
    } else {
        passive->status = OrderStatus::PartiallyFilled;
    }

    return {fill_qty, passive_id};
}

} // namespace hft
