#pragma once

#include "engine/types.hpp"
#include <cstdint>

namespace hft {

// ─── Trade ───────────────────────────────────────────────────────────────────
//
// Generated every time the matching engine produces a fill.
// One aggressor order can produce multiple Trade records
// (one per passive order it matches against).
//
struct Trade {
    uint64_t  trade_id;          // monotonically increasing
    OrderId   aggressor_id;      // order that triggered the match
    OrderId   passive_id;        // resting order that was hit
    Price     price;             // execution price (= passive order's price)
    Quantity  quantity;          // filled quantity
    Side      aggressor_side;    // BUY = buyer aggressed, SELL = seller aggressed
    int64_t   timestamp_ns;      // nanoseconds since epoch
    uint8_t   _pad[7];
    // Total = 56 bytes; no static_assert needed — size will grow in Week 3
    //         when we add venue/symbol fields.
};

} // namespace hft
