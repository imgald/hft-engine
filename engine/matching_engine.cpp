#include "engine/matching_engine.hpp"
#include <chrono>

namespace hft {

// ─── prices_cross ────────────────────────────────────────────────────────────
//
// Determines whether an aggressor order's price is willing to trade
// against a passive order at passive_price.
//
// Buy  aggressor crosses if: aggressor.price >= passive_price (ask)
//   e.g. buyer willing to pay 101, ask is 100 → crosses ✓
//        buyer willing to pay  99, ask is 100 → no cross ✗
//
// Sell aggressor crosses if: aggressor.price <= passive_price (bid)
//   e.g. seller wants 99,  bid is 100 → crosses ✓
//        seller wants 101, bid is 100 → no cross ✗
//
// Market orders always cross (Buy uses INT64_MAX, Sell uses 0 as sentinel).
//
bool MatchingEngine::prices_cross(const Order* aggressor,
                                  Price passive_price) const noexcept {
    if (aggressor->side == Side::Buy) {
        return aggressor->price >= passive_price;
    } else {
        return aggressor->price <= passive_price;
    }
}

// ─── match ───────────────────────────────────────────────────────────────────
//
// Core matching loop.
//
// Walk the opposite side of the book from best price inward,
// filling the aggressor order until:
//   (a) it is fully filled, or
//   (b) no more prices cross, or
//   (c) the opposite side is empty.
//
// For each passive order encountered:
//   - Calculate fill quantity = min(aggressor.remaining, passive.remaining)
//   - Generate a Trade record
//   - Update quantities on both sides
//   - Remove passive order from book if fully filled
//
void MatchingEngine::match(Order* aggressor, MatchResult& result) {
    // Choose the opposite side of the book to match against.
    // We need non-const access to modify quantities.
    // We reach into the book via friend-like access through OrderBook's
    // public cancel/add interface — in Week 4 we'll expose an iterator
    // interface for zero-copy access.
    //
    // For now we use the depth snapshot to find matchable prices,
    // then operate on the book directly.
    //
    // NOTE: This is intentionally simple for Week 2.
    //       Week 4 will replace the snapshot loop with direct iterator access.

    while (aggressor->remaining_qty > 0) {

        // ── Step 1: find the best opposing price ──────────────
        std::optional<Price> best_opposing;
        if (aggressor->side == Side::Buy) {
            best_opposing = book_.best_ask();
        } else {
            best_opposing = book_.best_bid();
        }

        // No opposing orders — stop matching.
        if (!best_opposing.has_value()) break;

        // Price doesn't cross — stop matching.
        if (!prices_cross(aggressor, *best_opposing)) break;

        // ── Step 2: get the front order at that price ─────────
        // We access PriceLevels through the book's internal maps.
        // OrderBook exposes a fill_front() helper we add below.
        auto [filled_qty, passive_id] =
            book_.fill_front(*best_opposing,
                             aggressor->side == Side::Buy ? Side::Sell : Side::Buy,
                             aggressor->remaining_qty);

        if (filled_qty == 0) break;   // should not happen, but defensive

        // ── Step 3: update aggressor ──────────────────────────
        aggressor->remaining_qty -= filled_qty;
        if (aggressor->remaining_qty == 0) {
            aggressor->status = OrderStatus::Filled;
        } else {
            aggressor->status = OrderStatus::PartiallyFilled;
        }

        // ── Step 4: record the trade ──────────────────────────
        Trade t{};
        t.trade_id       = next_trade_id();
        t.aggressor_id   = aggressor->order_id;
        t.passive_id     = passive_id;
        t.price          = *best_opposing;   // execution at passive price
        t.quantity       = filled_qty;
        t.aggressor_side = aggressor->side;
        t.timestamp_ns   = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        result.trades.push_back(t);
        book_.record_trade(filled_qty);
    }
}

// ─── process_order ───────────────────────────────────────────────────────────
//
// Full order lifecycle:
//   1. Try to match against the opposite side (for LIMIT, MARKET, IOC)
//   2. If remainder > 0:
//      - LIMIT  → rest on book
//      - MARKET → discard (market orders never rest)
//      - IOC    → cancel remainder
//
MatchResult MatchingEngine::process_order(Order* order) {
    MatchResult result;
    result.resting   = false;
    result.cancelled = false;

    // ── Phase 1: matching ─────────────────────────────────────
    match(order, result);

    // ── Phase 2: handle remainder ─────────────────────────────
    if (order->remaining_qty > 0) {
        switch (order->type) {
            case OrderType::Limit:
                // Rest unfilled portion on the book.
                book_.add_order(order);
                result.resting = true;
                break;

            case OrderType::Market:
                // Market orders never rest — discard remainder silently.
                order->status  = OrderStatus::Cancelled;
                result.cancelled = true;
                break;

            case OrderType::IOC:
                // Cancel unfilled remainder explicitly.
                order->status  = OrderStatus::Cancelled;
                result.cancelled = true;
                break;
        }
    }

    return result;
}

// ─── cancel_order ────────────────────────────────────────────────────────────

bool MatchingEngine::cancel_order(OrderId id) {
    return book_.cancel_order(id);
}

} // namespace hft
