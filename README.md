# Low Latency Order Matching Engine

A high-performance order matching engine written in C++20, targeting sub-microsecond
order processing latency. Built as a portfolio project for electronic trading and
HFT firm interviews.

## Architecture

```
┌─────────────────┐     WebSocket/JSON    ┌──────────────────┐
│  React Frontend │ ◄──────────────────► │  Node.js Bridge  │
│  (localhost:3000)│                      │  (localhost:8080) │
└─────────────────┘                      └────────┬─────────┘
                                                   │ TCP / FIX 4.2
                                          ┌────────▼─────────┐
                                          │   C++ Engine     │
                                          │  (localhost:9001) │
                                          │                  │
                                          │  ┌────────────┐  │
                                          │  │ OrderBook  │  │
                                          │  │ PriceLevel │  │
                                          │  │ Matching   │  │
                                          │  │ Engine     │  │
                                          │  └────────────┘  │
                                          └──────────────────┘
```

## Key Design Decisions

### 1. Fixed-Point Prices (`int64_t`, 4 decimal places)
Floating-point arithmetic is non-associative — `0.1 + 0.2 != 0.3` in IEEE 754,
which causes incorrect order-book bucketing. All prices are stored as integers:
`$189.42 → 1,894,200` with `PRICE_SCALE = 10,000`.

### 2. Price-Time Priority (FIFO) Matching
Standard exchange matching algorithm:
- **Price priority**: best price matches first (highest bid, lowest ask)
- **Time priority**: at equal prices, earlier orders match first

### 3. O(1) Cancel via Iterator Map
`PriceLevel` maintains a `std::list<Order*>` (stable iterators) plus an
`unordered_map<OrderId, list::iterator>` index. Cancel = one hash lookup +
one `list::erase` = O(1), regardless of queue depth.

This is structurally identical to LRU Cache — the same pattern of
linked list + hash map for O(1) ordered deletion.

### 4. `std::map` for Price Levels
`std::map<Price, PriceLevel, std::greater<>>` for bids gives `begin() ==
best bid` in O(1). Alternative (`unordered_map` + separate best-price variable)
was rejected because ordered iteration is needed for depth snapshots.

### 5. Cache-Line-Aligned Order Struct
`alignas(64)` ensures Order starts on a 64-byte cache line boundary.
Hot fields (price, qty, side) occupy the first cache line; cold fields
(timestamp, status) sit on the second.

### 6. Object Pool Allocator
Eliminates `malloc/new` on the hot path. The pool pre-allocates a flat
array of Order slots and maintains a free-list stack of indices.
`allocate()` and `free()` are O(1) with no syscalls.

Result: **p99.9 latency spikes reduced from ~1800ns to ~1173ns**.

### 7. epoll Non-Blocking I/O
Single-threaded event loop using Linux `epoll` with edge-triggered
notifications. `TCP_NODELAY` disables Nagle's algorithm for immediate
packet delivery.

### 8. Branch Prediction Hints
Hot-path branches annotated with `[[likely]]` / `[[unlikely]]` to guide
compiler instruction layout and reduce CPU pipeline flushes.

## Benchmark Results

Measured on WSL2 Ubuntu 24.04, Intel Core i7, 3.54 GHz, using `rdtscp`
for nanosecond-precision timing.

### Latency (in-process, no network overhead)

| Operation | Heap p99 | Pool p99 | Improvement |
|-----------|---------|---------|-------------|
| Limit order (resting) | 141ns | 111ns | +21% |
| Cancel order | 141ns | 103ns | +27% |
| Limit order (fill) | 79ns | 79ns | baseline already fast |

### Tail Latency (p99.9) — most important for HFT

| Operation | Heap p99.9 | Pool p99.9 | Improvement |
|-----------|-----------|-----------|-------------|
| Limit resting | ~1800ns | ~1173ns | **-35%** |
| Cancel | ~1682ns | ~252ns | **-85%** |

### Throughput

| Allocator | Orders/sec | Avg latency |
|-----------|-----------|-------------|
| Heap (`new`) | 9.74M | 102ns |
| Object Pool | 13.59M | 73ns |

**+39% throughput** from eliminating heap allocation on the hot path.

## Project Structure

```
hft/
├── engine/
│   ├── types.hpp              # Price (int64 fixed-point), Side, OrderType
│   ├── order.hpp              # Cache-line-aligned Order struct (128 bytes)
│   ├── order_book.hpp/cpp     # Dual-sided std::map order book
│   ├── price_level.hpp        # FIFO queue + O(1) cancel
│   ├── matching_engine.hpp/cpp # Price-time priority matching
│   ├── object_pool.hpp        # Fixed-size pool allocator
│   └── order_id_generator.hpp # Atomic monotonic counter
├── network/
│   ├── fix_parser.hpp/cpp     # FIX 4.2 parser/serializer
│   └── tcp_server.hpp/cpp     # epoll non-blocking TCP server
├── frontend/
│   ├── server.js              # Node.js WebSocket bridge
│   └── src/App.jsx            # React real-time dashboard
├── tests/                     # Google Test unit tests (72 tests)
├── bench/                     # rdtscp latency benchmarks
└── main.cpp                   # Server entry point
```

## Build & Run

**Requirements:** CMake >= 3.20, GCC >= 13, Node.js >= 18, Linux (epoll)

```bash
# Build and test
./scripts/build.sh test    # 72 unit tests
./scripts/build.sh bench   # latency benchmark

# Run full system (3 terminals)
./build/Release/bin/hft_server
cd frontend && node server.js
cd frontend && node node_modules/.bin/vite

# Open http://localhost:3000
```

## Testing

72 unit tests, 100% pass rate. Key coverage:
- Price-time FIFO ordering
- Partial fills and multi-level sweeps
- O(1) cancel (middle-of-queue removal)
- Fixed-point arithmetic correctness
- FIX 4.2 parse/serialize round-trip

## Known Limitations & Future Work

| Area | Current | Production approach |
|------|---------|-------------------|
| Network buffer | `std::string` | Fixed-size ring buffer |
| Container allocator | System heap | Custom pool for `std::list`/`std::map` nodes |
| Threading | Single-threaded | Lock-free SPSC queues between cores |
| Network layer | TCP | DPDK kernel-bypass |
| Protocol | Simplified FIX 4.2 | Full FIX + ITCH/OUCH |
