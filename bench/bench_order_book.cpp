#include "engine/matching_engine.hpp"
#include "engine/object_pool.hpp"
#include "engine/order_id_generator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <numeric>
#include <vector>

// ─── rdtscp ──────────────────────────────────────────────────────────────────
#if defined(__x86_64__) || defined(_M_X64)
  #include <x86intrin.h>
  static inline uint64_t tsc_now() { unsigned a; return __rdtscp(&a); }
  #define HFT_HAS_TSC 1
#else
  static inline uint64_t tsc_now() {
      return static_cast<uint64_t>(
          std::chrono::high_resolution_clock::now().time_since_epoch().count());
  }
  #define HFT_HAS_TSC 0
#endif

static double calibrate() {
    using C = std::chrono::high_resolution_clock;
    auto w0=C::now(); auto t0=tsc_now();
    while (C::now()-w0 < std::chrono::milliseconds(200)) {}
    double elapsed=std::chrono::duration<double,std::nano>(C::now()-w0).count();
    return (double)(tsc_now()-t0)/elapsed;
}

// ─── Stats ───────────────────────────────────────────────────────────────────
struct Stats { double p50,p99,p999,avg; };
static Stats compute(std::vector<double>& v) {
    std::sort(v.begin(),v.end());
    double sum=0; for(auto x:v) sum+=x;
    size_t n=v.size();
    auto p=[&](double pct){ return v[std::min((size_t)(n*pct/100.0),n-1)]; };
    return {p(50),p(99),p(99.9),sum/n};
}
static void print(const char* label, Stats s, Stats* base=nullptr) {
    printf("  %-36s  p50=%6.1f  p99=%6.1f  p99.9=%7.1f  [ns]",
           label, s.p50, s.p99, s.p999);
    if (base) printf("  %+.0f%% p99", (base->p99-s.p99)/base->p99*100.0);
    printf("\n");
}

// ─── Helpers ─────────────────────────────────────────────────────────────────
using namespace hft;
static OrderIdGenerator g_gen;

// Pool type: always heap-allocated via unique_ptr to avoid stack overflow.
// Max pool size we'll use: 25000 × 128 bytes = 3.1 MB — safe on heap.
constexpr size_t MAX_POOL = 25'000;
using Pool = ObjectPool<Order, MAX_POOL>;

static Order* heap_lmt(Side s, double px, Quantity q) {
    return new Order(Order::make_limit(g_gen.next(),s,to_price(px),q));
}
static Order* heap_mkt(Side s, Quantity q) {
    return new Order(Order::make_market(g_gen.next(),s,q));
}
static Order* pool_lmt(Pool& pool, Side s, double px, Quantity q) {
    Order* o = pool.make(Order::make_limit(g_gen.next(),s,to_price(px),q));
    if (!o) { fprintf(stderr,"POOL EXHAUSTED at in_use=%zu\n",pool.in_use()); exit(1); }
    return o;
}
static Order* pool_mkt(Pool& pool, Side s, Quantity q) {
    Order* o = pool.make(Order::make_market(g_gen.next(),s,q));
    if (!o) { fprintf(stderr,"POOL EXHAUSTED at in_use=%zu\n",pool.in_use()); exit(1); }
    return o;
}

// ─── Main ────────────────────────────────────────────────────────────────────
int main() {
    printf("\n══════════════════════════════════════════════════════════════════\n");
    printf("  HFT Engine — Week 4 Benchmark  (heap vs object pool)\n");
    printf("══════════════════════════════════════════════════════════════════\n");
    printf("  TSC  : %s\n", HFT_HAS_TSC ? "rdtscp" : "chrono");
    const double cpns = calibrate();
    printf("  CPU  : %.3f GHz\n", cpns);
    printf("  Pool : heap-allocated, %zu orders × %zu bytes = %zu MB\n\n",
           MAX_POOL, sizeof(Order), MAX_POOL*sizeof(Order)/1024/1024);

    auto ns=[&](uint64_t c){ return (double)c/cpns; };
    constexpr int N = 8'000;   // fits well within MAX_POOL

    // ══ 1. limit order, no match (resting) ═══════════════════════════════
    printf("  ── limit order, no match (resting) ──────────────────────────\n");
    Stats base_rest, pool_rest;
    {
        std::vector<double> lat; lat.reserve(N);
        MatchingEngine e("AAPL");
        for(int i=0;i<N;++i){
            Order* o=heap_lmt(i%2?Side::Buy:Side::Sell,100.0-(i%20)*0.01,100);
            auto t0=tsc_now(); e.process_order(o); lat.push_back(ns(tsc_now()-t0));
        }
        base_rest=compute(lat); print("heap alloc (baseline)",base_rest);
    }
    {
        std::vector<double> lat; lat.reserve(N);
        auto pool=std::make_unique<Pool>();
        MatchingEngine e("AAPL");
        for(int i=0;i<N;++i){
            Order* o=pool_lmt(*pool,i%2?Side::Buy:Side::Sell,100.0-(i%20)*0.01,100);
            auto t0=tsc_now(); e.process_order(o); lat.push_back(ns(tsc_now()-t0));
        }
        pool_rest=compute(lat); print("pool alloc (week4)   ",pool_rest,&base_rest);
        printf("    pool in_use=%zu/%zu\n", pool->in_use(), pool->capacity());
    }

    // ══ 2. limit order, full fill ════════════════════════════════════════
    // Orders are fully matched and consumed each iteration — reuse 2 slots.
    printf("\n  ── limit order, full fill (1 level) ─────────────────────────\n");
    Stats base_fill, pool_fill;
    {
        std::vector<double> lat; lat.reserve(N);
        for(int i=0;i<N;++i){
            MatchingEngine e("AAPL");
            auto* ask=heap_lmt(Side::Sell,100.0,100);
            e.process_order(ask);
            auto* bid=heap_lmt(Side::Buy,100.0,100);
            auto t0=tsc_now(); e.process_order(bid); lat.push_back(ns(tsc_now()-t0));
        }
        base_fill=compute(lat); print("heap alloc (baseline)",base_fill);
    }
    {
        std::vector<double> lat; lat.reserve(N);
        auto pool=std::make_unique<Pool>();
        for(int i=0;i<N;++i){
            MatchingEngine e("AAPL");
            auto* ask=pool_lmt(*pool,Side::Sell,100.0,100);
            e.process_order(ask);
            auto* bid=pool_lmt(*pool,Side::Buy,100.0,100);
            auto t0=tsc_now(); e.process_order(bid); lat.push_back(ns(tsc_now()-t0));
            pool->destroy(ask);
            pool->destroy(bid);
        }
        pool_fill=compute(lat); print("pool alloc (week4)   ",pool_fill,&base_fill);
    }

    // ══ 3. cancel order ══════════════════════════════════════════════════
    // N orders resting, then cancelled. Pool holds all N simultaneously.
    printf("\n  ── cancel order ──────────────────────────────────────────────\n");
    Stats base_cancel, pool_cancel;
    {
        std::vector<double> lat; lat.reserve(N);
        MatchingEngine e("AAPL");
        std::vector<OrderId> ids; ids.reserve(N);
        for(int i=0;i<N;++i){
            auto* o=heap_lmt(Side::Buy,100.0-(i%20)*0.01,100);
            ids.push_back(o->order_id); e.process_order(o);
        }
        for(auto id:ids){
            auto t0=tsc_now(); e.cancel_order(id); lat.push_back(ns(tsc_now()-t0));
        }
        base_cancel=compute(lat); print("heap alloc (baseline)",base_cancel);
    }
    {
        std::vector<double> lat; lat.reserve(N);
        auto pool=std::make_unique<Pool>();
        MatchingEngine e("AAPL");
        std::vector<OrderId> ids;  ids.reserve(N);
        std::vector<Order*>  ptrs; ptrs.reserve(N);
        for(int i=0;i<N;++i){
            auto* o=pool_lmt(*pool,Side::Buy,100.0-(i%20)*0.01,100);
            ids.push_back(o->order_id); ptrs.push_back(o); e.process_order(o);
        }
        for(size_t i=0;i<ids.size();++i){
            auto t0=tsc_now(); e.cancel_order(ids[i]); lat.push_back(ns(tsc_now()-t0));
            pool->destroy(ptrs[i]);
        }
        pool_cancel=compute(lat); print("pool alloc (week4)   ",pool_cancel,&base_cancel);
        printf("    pool in_use=%zu/%zu\n", pool->in_use(), pool->capacity());
    }

    // ══ 4. throughput — balanced order flow ══════════════════════════════
    // Alternate: add limit, add limit, cancel one → book stays bounded.
    // Pool never accumulates more than ~N/2 orders simultaneously.
    printf("\n  ── Throughput (500k orders, balanced flow) ───────────────────\n");
    constexpr int M = 8'000;
    {
        MatchingEngine e("AAPL");
        auto w0=std::chrono::high_resolution_clock::now();
        std::vector<OrderId> live_ids;
        live_ids.reserve(1000);
        for(int i=0;i<M;++i){
            if (i%3==0){
                auto* o=heap_lmt(Side::Buy, 100.0-(i%10)*0.01,10);
                live_ids.push_back(o->order_id); e.process_order(o);
            } else if (i%3==1){
                auto* o=heap_lmt(Side::Sell,100.0+(i%10)*0.01,10);
                live_ids.push_back(o->order_id); e.process_order(o);
            } else if (!live_ids.empty()){
                e.cancel_order(live_ids.back()); live_ids.pop_back();
            }
        }
        double s=std::chrono::duration<double>(std::chrono::high_resolution_clock::now()-w0).count();
        printf("  heap : %.2f M orders/sec  (avg %.1fns)\n",M/s/1e6,s*1e9/M);
    }
    {
        auto pool=std::make_unique<Pool>();
        MatchingEngine e("AAPL");
        auto w0=std::chrono::high_resolution_clock::now();
        std::vector<OrderId> live_ids;
        std::vector<Order*>  live_ptrs;
        live_ids.reserve(1000); live_ptrs.reserve(1000);
        for(int i=0;i<M;++i){
            if (i%3==0){
                auto* o=pool_lmt(*pool,Side::Buy, 100.0-(i%10)*0.01,10);
                live_ids.push_back(o->order_id); live_ptrs.push_back(o); e.process_order(o);
            } else if (i%3==1){
                auto* o=pool_lmt(*pool,Side::Sell,100.0+(i%10)*0.01,10);
                live_ids.push_back(o->order_id); live_ptrs.push_back(o); e.process_order(o);
            } else if (!live_ids.empty()){
                e.cancel_order(live_ids.back()); live_ids.pop_back();
                pool->destroy(live_ptrs.back()); live_ptrs.pop_back();
            }
        }
        double s=std::chrono::duration<double>(std::chrono::high_resolution_clock::now()-w0).count();
        printf("  pool : %.2f M orders/sec  (avg %.1fns)\n",M/s/1e6,s*1e9/M);
        printf("  pool in_use=%zu/%zu (peak estimate)\n", pool->in_use(), pool->capacity());
    }

    printf("\n");
    return 0;
}
