# HFT Matching Engine

A low-latency order matching engine written in C++20, targeting sub-microsecond
order processing latency.  Built as a portfolio project for electronic trading
and HFT firm interviews (Jane Street, Citadel, Jump Trading).

## Architecture

```
hft/
├── engine/
│   ├── types.hpp          # Price (fixed-point int64), Side, OrderType
│   ├── order.hpp          # Cache-line-aligned Order struct (128 bytes)
│   ├── trade.hpp          # Execution report
│   ├── price_level.hpp    # FIFO queue at one price point; O(1) cancel
│   ├── order_book.hpp     # Dual-sided book; std::map for sorted price levels
│   └── order_id_generator.hpp
├── tests/                 # Google Test unit tests
├── bench/                 # rdtsc-based latency benchmarks
└── scripts/build.sh
```

## Key Design Decisions

### Fixed-Point Prices (`int64_t`, 4 decimal places)
Floating-point arithmetic is non-associative.  `0.1 + 0.2 != 0.3` in IEEE 754,
which causes incorrect order-book bucketing.  All prices are stored as integers:
`$189.42 → 1,894,200`.

### Cache-Line-Aligned Order Struct
`alignas(64)` ensures the Order starts on a 64-byte cache line boundary.
Hot fields (price, qty, side) occupy the first cache line; cold fields
(timestamp, status) sit on the second.  A matching engine tick only
dirtying one cache line per order.

### O(1) Cancel via Iterator Map
`PriceLevel` maintains a `std::list<Order*>` (stable iterators) plus an
`unordered_map<OrderId, list::iterator>` index.  Cancel = one hash lookup +
one `list::erase` = O(1), regardless of queue depth.

### `std::map` for Price Levels
`std::map<Price, PriceLevel, std::greater<>>` for bids gives `begin() ==
best bid` in O(1).  Alternative (`unordered_map` + separate best-price
variable) was rejected because ordered iteration is needed for depth
snapshots, and `std::map` over ~20 price levels is cache-friendly.

## Roadmap

| Week | Deliverable |
|------|-------------|
| 1    | ✅ Project skeleton, Order struct, OrderBook, unit tests |
| 2    | PriceLevel polish, OrderBook cancel improvements |
| 3    | Matching engine (LIMIT / MARKET / IOC), Trade records |
| 4    | Object pool, cache tuning, perf profiling |
| 5–6  | FIX protocol parser, epoll TCP server, WebSocket feed |
| 7    | React frontend visualization |
| 8    | Stress test, README, benchmark report |

## Build

```bash
# Release build
./scripts/build.sh

# Debug build (AddressSanitizer + UBSan)
./scripts/build.sh debug

# Build + run tests
./scripts/build.sh test

# Build + run benchmark
./scripts/build.sh bench
```

**Requirements:** CMake ≥ 3.20, GCC ≥ 12 or Clang ≥ 15, internet access
(FetchContent downloads Google Test).

## Benchmark (Week 1 baseline)

```
add_order (limit, no match)   avg=  85ns  p50=  72ns  p99= 180ns
cancel_order                  avg=  90ns  p50=  78ns  p99= 195ns
best_bid() + best_ask()       avg=   8ns  p50=   6ns  p99=  18ns
```

Target after Week 4 optimizations: P99 < 100ns for add/cancel.
