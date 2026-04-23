#pragma once

#include "engine/types.hpp"
#include <cstdint>
#include <chrono>

namespace hft {

// ─── Order ───────────────────────────────────────────────────────────────────
//
// Memory layout is intentional:
//
//   CACHE LINE 1 (64 bytes) — "hot" fields read/written on every match tick
//   ┌────────────┬──────────────┬──────────────┬────────┬──────────┬─────────┐
//   │  order_id  │    price     │   quantity   │  rem   │  side    │  type   │
//   │  8 bytes   │   8 bytes    │   8 bytes    │ 8 bytes│  1 byte  │  1 byte │
//   └────────────┴──────────────┴──────────────┴────────┴──────────┴─────────┘
//   Total hot = 34 bytes, fits comfortably in 64 bytes.
//
//   CACHE LINE 2 — "cold" fields only touched at order entry / reporting
//   ┌───────────────────┬────────────┐
//   │    timestamp_ns   │  status    │
//   │     8 bytes       │  1 byte    │
//   └───────────────────┴────────────┘
//
// alignas(64) guarantees the struct starts on a cache-line boundary so the
// hot fields never straddle two cache lines.
//
struct alignas(64) Order {
    // ── Hot fields (cache line 1) ─────────────────────────────
    OrderId     order_id;           // unique order identifier
    Price       price;              // fixed-point (see types.hpp)
    Quantity    quantity;           // original order quantity
    Quantity    remaining_qty;      // decremented on each fill
    Side        side;               // Buy or Sell
    OrderType   type;               // Limit / Market / IOC
    uint8_t     _pad1[6];           // explicit padding (not left to compiler)

    // ── Cold fields (cache line 2) ────────────────────────────
    int64_t     timestamp_ns;       // nanoseconds since epoch (filled at entry)
    OrderStatus status;
    uint8_t     _pad2[55];          // pad to exactly 128 bytes total

    // ─── Factory helpers ──────────────────────────────────────
    static Order make_limit(OrderId id, Side side, Price price, Quantity qty) noexcept {
        Order o{};
        o.order_id      = id;
        o.price         = price;
        o.quantity      = qty;
        o.remaining_qty = qty;
        o.side          = side;
        o.type          = OrderType::Limit;
        o.status        = OrderStatus::New;
        o.timestamp_ns  = now_ns();
        return o;
    }

    static Order make_market(OrderId id, Side side, Quantity qty) noexcept {
        Order o{};
        o.order_id      = id;
        // Market orders use sentinel prices:
        //   Buy  → max price  (will match anything on ask side)
        //   Sell → 0          (will match anything on bid side)
        o.price         = (side == Side::Buy) ? INT64_MAX : 0;
        o.quantity      = qty;
        o.remaining_qty = qty;
        o.side          = side;
        o.type          = OrderType::Market;
        o.status        = OrderStatus::New;
        o.timestamp_ns  = now_ns();
        return o;
    }

    static Order make_ioc(OrderId id, Side side, Price price, Quantity qty) noexcept {
        Order o = make_limit(id, side, price, qty);
        o.type = OrderType::IOC;
        return o;
    }

    // ─── Convenience predicates ───────────────────────────────
    bool is_filled()    const noexcept { return remaining_qty == 0; }
    bool is_active()    const noexcept {
        return status == OrderStatus::New ||
               status == OrderStatus::PartiallyFilled;
    }
    bool is_buy()       const noexcept { return side == Side::Buy; }
    bool is_sell()      const noexcept { return side == Side::Sell; }

private:
    static int64_t now_ns() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }
};

// Compile-time size assertions — catch layout regressions immediately.
static_assert(sizeof(Order) == 128,
    "Order must be exactly 128 bytes (2 cache lines)");
static_assert(alignof(Order) == 64,
    "Order must be aligned to a 64-byte cache line boundary");
static_assert(offsetof(Order, timestamp_ns) >= 32,
    "Cold fields must start after the first 32 bytes");

} // namespace hft
