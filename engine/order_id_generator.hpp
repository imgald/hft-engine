#pragma once

#include "engine/types.hpp"
#include <atomic>

namespace hft {

// ─── OrderIdGenerator ─────────────────────────────────────────────────────────
//
// Produces monotonically increasing OrderIds.
//
// Uses std::atomic with memory_order_relaxed: we only need uniqueness,
// not ordering guarantees relative to other memory operations.
// This is the fastest atomic increment possible on x86.
//
class OrderIdGenerator {
public:
    explicit OrderIdGenerator(OrderId start = 1) noexcept
        : counter_(start) {}

    OrderId next() noexcept {
        return counter_.fetch_add(1, std::memory_order_relaxed);
    }

    // Reset for testing purposes only.
    void reset(OrderId val = 1) noexcept {
        counter_.store(val, std::memory_order_relaxed);
    }

private:
    std::atomic<OrderId> counter_;
};

} // namespace hft
