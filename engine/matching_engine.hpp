#pragma once

#include "engine/order_book.hpp"
#include "engine/trade.hpp"
#include "engine/order_id_generator.hpp"
#include <vector>
#include <functional>

namespace hft {

// ─── MatchResult ─────────────────────────────────────────────────────────────
//
// Returned by processOrder() — everything the caller needs to know
// about what happened when an order was submitted.
//
struct MatchResult {
    std::vector<Trade> trades;      // all fills generated (empty if no match)
    bool               resting;     // true if order rested on the book
    bool               cancelled;   // true if IOC order was cancelled (unfilled remainder)
};

// ─── MatchingEngine ──────────────────────────────────────────────────────────
//
// Owns the OrderBook for one instrument and implements the matching algorithm.
//
// Matching algorithm: Price-Time Priority (FIFO)
//   1. Price priority  — best price matches first
//                        (highest bid, lowest ask)
//   2. Time priority   — at the same price, earlier orders match first
//                        (enforced by PriceLevel's FIFO queue)
//
// Supported order types:
//   LIMIT  — match up to the limit price; rest remainder on book
//   MARKET — match at any price; never rests (cancelled if unmatched)
//   IOC    — Immediate-Or-Cancel: same as LIMIT but remainder cancelled
//
class MatchingEngine {
public:
    explicit MatchingEngine(std::string symbol)
        : book_(std::move(symbol)) {}

    // ── Core operation ────────────────────────────────────────
    //
    // Submit an order to the engine.
    // The engine takes ownership of the Order object.
    // Returns a MatchResult describing all fills and the order's final state.
    //
    MatchResult process_order(Order* order);

    // Cancel a resting order by ID.
    // Returns true if found and cancelled.
    bool cancel_order(OrderId id);

    // ── Accessors ─────────────────────────────────────────────
    const OrderBook& book()   const noexcept { return book_; }
    uint64_t         trade_id_counter() const noexcept { return next_trade_id_; }

private:
    // ── Matching logic ────────────────────────────────────────

    // Try to match `order` against the opposite side of the book.
    // Fills `result.trades` with any executions.
    // Decrements order->remaining_qty as fills occur.
    void match(Order* order, MatchResult& result);

    // Returns true if `aggressor` can match against `passive`.
    // i.e. the prices cross.
    bool prices_cross(const Order* aggressor, Price passive_price) const noexcept;

    // Generate the next trade ID (monotonically increasing).
    uint64_t next_trade_id() noexcept { return next_trade_id_++; }

    // ── State ─────────────────────────────────────────────────
    OrderBook book_;
    uint64_t  next_trade_id_ = 1;
};

} // namespace hft
