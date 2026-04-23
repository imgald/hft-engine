#pragma once

#include "engine/price_level.hpp"
#include "engine/trade.hpp"
#include <map>
#include <unordered_map>
#include <functional>
#include <vector>
#include <string>
#include <optional>

namespace hft {

// ─── OrderBook ───────────────────────────────────────────────────────────────
//
// Maintains the full limit-order book for a single instrument.
//
// Container choice for the price-sorted maps:
//
//   Bids: std::map<Price, PriceLevel, std::greater<Price>>
//     • begin() == best (highest) bid  → O(1) best-bid lookup
//     • Insertion/deletion: O(log n) where n = number of distinct price levels
//       (typically very small, ~10–50 in liquid markets)
//
//   Asks: std::map<Price, PriceLevel, std::less<Price>>  (default)
//     • begin() == best (lowest) ask   → O(1) best-ask lookup
//
//   Alternative considered: std::unordered_map + maintaining a separate
//   best-price variable.  Rejected because:
//     • We need ordered iteration for depth snapshots (top N levels).
//     • Maintaining best-price manually adds complexity and bug surface.
//     • std::map log(n) is fast when n is small (cache-friendly B-tree nodes).
//
// Order lookup for cancel:
//   std::unordered_map<OrderId, Price> order_location_
//     Maps order_id → its price level so we can find and cancel in O(log n).
//
class OrderBook {
public:
    explicit OrderBook(std::string symbol) noexcept
        : symbol_(std::move(symbol)) {}

    // ── Book queries ──────────────────────────────────────────

    const std::string& symbol() const noexcept { return symbol_; }

    // Best bid price (highest).  nullopt if book is empty.
    std::optional<Price> best_bid() const noexcept {
        if (bids_.empty()) return std::nullopt;
        return bids_.begin()->first;
    }

    // Best ask price (lowest).  nullopt if book is empty.
    std::optional<Price> best_ask() const noexcept {
        if (asks_.empty()) return std::nullopt;
        return asks_.begin()->first;
    }

    // Spread in price ticks.  nullopt if either side is empty.
    std::optional<Price> spread() const noexcept {
        auto b = best_bid(), a = best_ask();
        if (!b || !a) return std::nullopt;
        return *a - *b;
    }

    // Mid price * 2 (avoids fractional tick).  nullopt if either side empty.
    std::optional<Price> mid_price_x2() const noexcept {
        auto b = best_bid(), a = best_ask();
        if (!b || !a) return std::nullopt;
        return *b + *a;   // caller divides by 2 if needed
    }

    bool has_order(OrderId id) const noexcept {
        return order_location_.count(id) > 0;
    }

    // Total resting quantity on one side at a given price.
    Quantity qty_at(Side side, Price price) const noexcept {
        if (side == Side::Buy) {
            auto it = bids_.find(price);
            return it == bids_.end() ? 0 : it->second.total_qty();
        } else {
            auto it = asks_.find(price);
            return it == asks_.end() ? 0 : it->second.total_qty();
        }
    }

    // ── Depth snapshot ────────────────────────────────────────

    struct Level {
        Price    price;
        Quantity qty;
        size_t   order_count;
    };

    // Returns up to `n` best bid levels (price descending).
    std::vector<Level> bid_levels(size_t n = 10) const {
        return snapshot(bids_, n);
    }

    // Returns up to `n` best ask levels (price ascending).
    std::vector<Level> ask_levels(size_t n = 10) const {
        return snapshot(asks_, n);
    }

    // ── Mutators (used by MatchingEngine) ─────────────────────

    // Add a resting order to the book.
    // Caller must ensure the order is not immediately matchable
    // (i.e., matching engine has already tried to match it).
    void add_order(Order* order);

    // Cancel a resting order.  Returns true if found and removed.
    bool cancel_order(OrderId id);

    // ── Statistics ────────────────────────────────────────────

    uint64_t total_volume()    const noexcept { return total_volume_; }
    uint64_t trade_count()     const noexcept { return trade_count_; }
    void record_trade(Quantity qty) noexcept {
        total_volume_ += static_cast<uint64_t>(qty);
        ++trade_count_;
    }

private:
    // Sorted bid side: highest price first.
    using BidMap = std::map<Price, PriceLevel, std::greater<Price>>;
    // Sorted ask side: lowest price first (default less<>).
    using AskMap = std::map<Price, PriceLevel>;

    template<typename Map>
    std::vector<Level> snapshot(const Map& m, size_t n) const {
        std::vector<Level> out;
        out.reserve(n);
        for (auto it = m.begin(); it != m.end() && out.size() < n; ++it) {
            out.push_back({ it->first,
                            it->second.total_qty(),
                            it->second.depth() });
        }
        return out;
    }

    std::string symbol_;
    BidMap      bids_;
    AskMap      asks_;

    // Fast order-id → price lookup for O(log n) cancel.
    std::unordered_map<OrderId, Price> order_location_;

    uint64_t    total_volume_ = 0;
    uint64_t    trade_count_  = 0;
};

} // namespace hft
