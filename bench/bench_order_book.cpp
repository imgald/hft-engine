#include "engine/matching_engine.hpp"
#include "engine/order_id_generator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <numeric>
#include <vector>

// ─── rdtscp ──────────────────────────────────────────────────────────────────
//
// rdtscp (serializing) vs rdtsc (non-serializing):
//   rdtsc  — CPU can reorder instructions across it → measurement noise
//   rdtscp — waits for all prior instructions to retire before reading
//             the counter → more accurate for microbenchmarks
//
#if defined(__x86_64__) || defined(_M_X64)
  #include <x86intrin.h>
  static inline uint64_t tsc_now() {
      unsigned aux;
      return __rdtscp(&aux);
  }
  #define HFT_HAS_TSC 1
#else
  static inline uint64_t tsc_now() {
      return static_cast<uint64_t>(
          std::chrono::high_resolution_clock::now().time_since_epoch().count());
  }
  #define HFT_HAS_TSC 0
#endif

// ─── Calibration ─────────────────────────────────────────────────────────────
static double calibrate_cycles_per_ns() {
    using Clock = std::chrono::high_resolution_clock;
    auto     w0 = Clock::now();
    uint64_t t0 = tsc_now();
    while (Clock::now() - w0 < std::chrono::milliseconds(200)) {}
    uint64_t t1 = tsc_now();
    auto     w1 = Clock::now();
    double elapsed_ns = std::chrono::duration<double, std::nano>(w1 - w0).count();
    return static_cast<double>(t1 - t0) / elapsed_ns;
}

// ─── Stats ───────────────────────────────────────────────────────────────────
struct Stats { double min, p50, p90, p99, p999, max, avg; };

static Stats compute(std::vector<double>& v) {
    std::sort(v.begin(), v.end());
    double sum = 0; for (auto x : v) sum += x;
    size_t n = v.size();
    auto p = [&](double pct) { return v[std::min((size_t)(n*pct/100.0), n-1)]; };
    return { v.front(), p(50), p(90), p(99), p(99.9), v.back(), sum/n };
}

static void print(const char* label, Stats s) {
    printf("  %-38s  avg=%6.1f  p50=%6.1f  p99=%6.1f  p99.9=%7.1f  [ns]\n",
           label, s.avg, s.p50, s.p99, s.p999);
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
using namespace hft;
static OrderIdGenerator g_gen;

static Order* lmt(Side s, double px, Quantity q) {
    return new Order(Order::make_limit(g_gen.next(), s, to_price(px), q));
}
static Order* mkt(Side s, Quantity q) {
    return new Order(Order::make_market(g_gen.next(), s, q));
}
static void seed(MatchingEngine& e, double mid, int levels) {
    for (int i = 1; i <= levels; ++i) {
        e.process_order(lmt(Side::Buy,  mid - i*0.01, 1000));
        e.process_order(lmt(Side::Sell, mid + i*0.01, 1000));
    }
}

// ─── Main ────────────────────────────────────────────────────────────────────
int main() {
    printf("\n══════════════════════════════════════════════════════════════\n");
    printf("  HFT Engine — Latency Benchmark  (Week 3 baseline)\n");
    printf("══════════════════════════════════════════════════════════════\n");
    printf("  TSC : %s\n", HFT_HAS_TSC ? "rdtscp (serializing)" : "chrono fallback");

    const double cpns = calibrate_cycles_per_ns();
    printf("  CPU : %.3f GHz\n\n", cpns);

    constexpr int W = 20'000;      // warmup
    constexpr int N = 200'000;     // samples

    auto ns = [&](uint64_t cycles) { return (double)cycles / cpns; };

    // ── 1. Limit order, no match ──────────────────────────────────────────
    {
        std::vector<double> lat; lat.reserve(N);
        { MatchingEngine e("W");
          for (int i=0;i<W;++i) e.process_order(lmt(i%2?Side::Buy:Side::Sell,100.0-(i%10)*0.01,100)); }

        MatchingEngine e("AAPL");
        std::vector<Order*> orders; orders.reserve(N);
        for (int i=0;i<N;++i)
            orders.push_back(lmt(i%2?Side::Buy:Side::Sell, 100.0-(i%20)*0.01, 100));

        for (auto* o : orders) {
            auto t0 = tsc_now(); e.process_order(o); auto t1 = tsc_now();
            lat.push_back(ns(t1-t0));
        }
        print("limit order, no match (resting)", compute(lat));
    }

    // ── 2. Limit order, full fill ─────────────────────────────────────────
    {
        std::vector<double> lat; lat.reserve(N);
        { MatchingEngine e("W");
          for (int i=0;i<W;++i){ e.process_order(lmt(Side::Sell,100.0,100));
                                  e.process_order(lmt(Side::Buy, 100.0,100)); } }

        for (int i=0;i<N;++i) {
            MatchingEngine e("AAPL");
            e.process_order(lmt(Side::Sell, 100.0, 100));
            Order* buy = lmt(Side::Buy, 100.0, 100);
            auto t0 = tsc_now(); e.process_order(buy); auto t1 = tsc_now();
            lat.push_back(ns(t1-t0));
        }
        print("limit order, full fill (1 level)", compute(lat));
    }

    // ── 3. Market order, sweep 5 levels ──────────────────────────────────
    {
        std::vector<double> lat; lat.reserve(N);
        { MatchingEngine e("W");
          for (int i=0;i<W;++i){ seed(e,100.0,5); e.process_order(mkt(Side::Buy,500)); } }

        for (int i=0;i<N;++i) {
            MatchingEngine e("AAPL");
            seed(e, 100.0, 5);
            Order* o = mkt(Side::Buy, 500);
            auto t0 = tsc_now(); e.process_order(o); auto t1 = tsc_now();
            lat.push_back(ns(t1-t0));
        }
        print("market order, sweep 5 levels", compute(lat));
    }

    // ── 4. Cancel order ───────────────────────────────────────────────────
    {
        std::vector<double> lat; lat.reserve(N);
        MatchingEngine e("AAPL");
        std::vector<OrderId> ids; ids.reserve(N);
        for (int i=0;i<N;++i) {
            auto* o = lmt(Side::Buy, 100.0-(i%20)*0.01, 100);
            ids.push_back(o->order_id);
            e.process_order(o);
        }
        for (auto id : ids) {
            auto t0 = tsc_now(); e.cancel_order(id); auto t1 = tsc_now();
            lat.push_back(ns(t1-t0));
        }
        print("cancel order", compute(lat));
    }

    // ── 5. Throughput ─────────────────────────────────────────────────────
    {
        printf("\n  ── Throughput ───────────────────────────────────────────\n");
        MatchingEngine e("AAPL");
        seed(e, 100.0, 10);
        constexpr int M = 1'000'000;
        auto w0 = std::chrono::high_resolution_clock::now();
        for (int i=0;i<M;++i) {
            if      (i%4==0) e.process_order(lmt(Side::Sell, 100.0+(i%10)*0.01, 10));
            else if (i%4==1) e.process_order(lmt(Side::Buy,  100.0-(i%10)*0.01, 10));
            else             e.process_order(mkt(i%2?Side::Buy:Side::Sell, 5));
        }
        auto w1  = std::chrono::high_resolution_clock::now();
        double s = std::chrono::duration<double>(w1-w0).count();
        printf("  %d orders in %.3fs  →  %.2f M orders/sec  (avg %.1fns)\n",
               M, s, M/s/1e6, s*1e9/M);
    }

    printf("\n  ── Week 4 targets ───────────────────────────────────────\n");
    printf("  limit resting  p99 < 200ns\n");
    printf("  limit fill     p99 < 500ns\n");
    printf("  cancel         p99 < 200ns\n\n");

    return 0;
}
