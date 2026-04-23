#include "engine/order_book.hpp"
#include "engine/order_id_generator.hpp"

#include <chrono>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <numeric>
#include <cstring>

// ─── rdtsc ────────────────────────────────────────────────────────────────────
//
// __rdtsc() reads the CPU's Time Stamp Counter — a 64-bit register incremented
// every clock cycle.  It's the gold standard for nanosecond-level microbenchmarks
// in HFT because:
//   1. It has ~1ns resolution (vs chrono's ~20–100ns overhead on some systems).
//   2. It doesn't involve a syscall (clock_gettime does on some kernels).
//
// Caveats:
//   - Not serializing: the CPU can reorder instructions across the rdtsc.
//     Use __rdtscp() or insert a memory fence if you need strict ordering.
//   - Not portable: x86 only.  Fine for HFT (always x86).
//   - Requires a calibration step to convert cycles → nanoseconds.
//
#if defined(__x86_64__) || defined(_M_X64)
  #include <x86intrin.h>
  #define HFT_RDTSC() __rdtsc()
  #define HFT_HAS_RDTSC 1
#else
  // Fallback for non-x86 (ARM, etc.)
  static inline uint64_t hft_rdtsc_fallback() {
      return static_cast<uint64_t>(
          std::chrono::high_resolution_clock::now().time_since_epoch().count());
  }
  #define HFT_RDTSC() hft_rdtsc_fallback()
  #define HFT_HAS_RDTSC 0
#endif

// ─── Calibrate cycles/ns ─────────────────────────────────────────────────────
static double cycles_per_ns() {
    auto t0 = std::chrono::high_resolution_clock::now();
    uint64_t c0 = HFT_RDTSC();
    // busy-wait 100ms
    while (std::chrono::high_resolution_clock::now() - t0 <
           std::chrono::milliseconds(100)) {}
    uint64_t c1 = HFT_RDTSC();
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed_ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return static_cast<double>(c1 - c0) / elapsed_ns;
}

// ─── Statistics helpers ───────────────────────────────────────────────────────
static double percentile(std::vector<double>& sorted, double pct) {
    size_t idx = static_cast<size_t>(sorted.size() * pct / 100.0);
    idx = std::min(idx, sorted.size() - 1);
    return sorted[idx];
}

static void print_stats(const char* label, std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    printf("  %-30s  avg=%6.1fns  p50=%6.1fns  p99=%6.1fns  min=%5.1fns  max=%6.1fns\n",
           label,
           sum / samples.size(),
           percentile(samples, 50),
           percentile(samples, 99),
           samples.front(),
           samples.back());
}

// ─── Benchmarks ───────────────────────────────────────────────────────────────

int main() {
    printf("\n");
    printf("════════════════════════════════════════════════════════\n");
    printf("  HFT Engine — Baseline Latency Benchmark (Week 1)\n");
    printf("════════════════════════════════════════════════════════\n");
    printf("  rdtsc available : %s\n", HFT_HAS_RDTSC ? "yes (x86)" : "no (fallback)");

    const double cpns = cycles_per_ns();
    printf("  CPU speed        : %.2f GHz\n\n", cpns);

    using namespace hft;
    OrderIdGenerator gen;
    constexpr int WARMUP  = 10'000;
    constexpr int SAMPLES = 100'000;

    // ── Bench 1: add_order (resting limit, no match) ──────────────────────
    {
        std::vector<double> latencies;
        latencies.reserve(SAMPLES);

        // Warmup
        OrderBook wb("WB");
        for (int i = 0; i < WARMUP; ++i) {
            auto* o = new Order(Order::make_limit(
                gen.next(), i % 2 == 0 ? Side::Buy : Side::Sell,
                to_price(100.0 - (i % 10) * 0.01), 100));
            wb.add_order(o);
        }

        // Measure
        OrderBook book("AAPL");
        std::vector<Order*> orders;
        orders.reserve(SAMPLES);
        for (int i = 0; i < SAMPLES; ++i) {
            orders.push_back(new Order(Order::make_limit(
                gen.next(), i % 2 == 0 ? Side::Buy : Side::Sell,
                to_price(100.0 - (i % 20) * 0.01), 100)));
        }

        for (auto* o : orders) {
            uint64_t t0 = HFT_RDTSC();
            book.add_order(o);
            uint64_t t1 = HFT_RDTSC();
            latencies.push_back(static_cast<double>(t1 - t0) / cpns);
        }
        print_stats("add_order (limit, no match)", latencies);
    }

    // ── Bench 2: cancel_order ─────────────────────────────────────────────
    {
        std::vector<double> latencies;
        latencies.reserve(SAMPLES);

        OrderBook book("AAPL");
        std::vector<OrderId> ids;
        ids.reserve(SAMPLES);

        for (int i = 0; i < SAMPLES; ++i) {
            OrderId id = gen.next();
            ids.push_back(id);
            book.add_order(new Order(Order::make_limit(
                id, Side::Buy, to_price(100.0 - (i % 10) * 0.01), 100)));
        }

        for (OrderId id : ids) {
            uint64_t t0 = HFT_RDTSC();
            book.cancel_order(id);
            uint64_t t1 = HFT_RDTSC();
            latencies.push_back(static_cast<double>(t1 - t0) / cpns);
        }
        print_stats("cancel_order", latencies);
    }

    // ── Bench 3: best_bid() / best_ask() lookup ───────────────────────────
    {
        std::vector<double> latencies;
        latencies.reserve(SAMPLES);

        OrderBook book("AAPL");
        for (int i = 0; i < 20; ++i) {
            book.add_order(new Order(Order::make_limit(
                gen.next(), Side::Buy,  to_price(100.0 - i * 0.01), 100)));
            book.add_order(new Order(Order::make_limit(
                gen.next(), Side::Sell, to_price(100.1 + i * 0.01), 100)));
        }

        volatile Price sink = 0;   // prevent dead-code elimination
        for (int i = 0; i < SAMPLES; ++i) {
            uint64_t t0 = HFT_RDTSC();
            auto bb = book.best_bid();
            auto ba = book.best_ask();
            uint64_t t1 = HFT_RDTSC();
            if (bb) sink += *bb;
            if (ba) sink += *ba;
            latencies.push_back(static_cast<double>(t1 - t0) / cpns);
        }
        print_stats("best_bid() + best_ask()", latencies);
    }

    printf("\n");
    printf("  NOTE: These are in-process latencies without network/OS overhead.\n");
    printf("  Target for matching engine (Week 3): P99 < 500ns.\n");
    printf("  Week 4 optimizations (pool alloc, cache alignment) will push\n");
    printf("  this toward P99 < 100ns.\n\n");

    return 0;
}
