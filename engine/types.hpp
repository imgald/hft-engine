#pragma once

#include <cstdint>
#include <string>

namespace hft {

// ─── Price (fixed-point) ──────────────────────────────────────────────────────
//
// WHY NOT double/float?
//   Floating-point arithmetic is non-associative and subject to rounding error.
//   Two prices that "look" equal may differ at the bit level, causing incorrect
//   order-book bucketing.  In production HFT systems prices are always integers.
//
// Convention: 1 unit = 0.0001 USD  (4 decimal places, "basis points of a cent")
//   $189.4200  →  1_894_200
//   $  0.0001  →          1
//
// int64_t gives us a range of ±922 trillion cents — more than sufficient.
//
using Price    = int64_t;
using Quantity = int64_t;
using OrderId  = uint64_t;

// Price helpers
constexpr int64_t PRICE_SCALE = 10'000;   // 4 decimal places

// Convert a human-readable double to our internal Price.
// Only use this at I/O boundaries (parsing FIX/JSON), never on the hot path.
inline Price to_price(double d) noexcept {
    return static_cast<Price>(d * PRICE_SCALE + 0.5);   // round half-up
}

inline double from_price(Price p) noexcept {
    return static_cast<double>(p) / PRICE_SCALE;
}

// ─── Side ────────────────────────────────────────────────────────────────────
enum class Side : uint8_t {
    Buy  = 0,
    Sell = 1,
};

inline Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

inline const char* to_string(Side s) noexcept {
    return s == Side::Buy ? "BUY" : "SELL";
}

// ─── OrderType ───────────────────────────────────────────────────────────────
enum class OrderType : uint8_t {
    Limit  = 0,   // rest on book if not immediately matchable
    Market = 1,   // match immediately at any price; never rests
    IOC    = 2,   // Immediate-Or-Cancel: fill what you can, cancel remainder
};

inline const char* to_string(OrderType t) noexcept {
    switch (t) {
        case OrderType::Limit:  return "LIMIT";
        case OrderType::Market: return "MARKET";
        case OrderType::IOC:    return "IOC";
    }
    return "UNKNOWN";
}

// ─── OrderStatus ─────────────────────────────────────────────────────────────
enum class OrderStatus : uint8_t {
    New           = 0,
    PartiallyFilled = 1,
    Filled        = 2,
    Cancelled     = 3,
    Rejected      = 4,
};

} // namespace hft
