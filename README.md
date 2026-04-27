# Low Latency Order Matching Engine

![CI](https://github.com/imgald/hft-engine/actions/workflows/ci.yml/badge.svg)
A high-performance order matching engine written in C++20, targeting sub-microsecond
order processing latency. Built as a portfolio project for electronic trading and
HFT firm interviews.

## Architecture

```
+-------------------+   WebSocket / JSON   +------------------+
|  React Frontend   | <------------------> |  Node.js Bridge  |
|  (localhost:3000) |                      |  (localhost:8080) |
+-------------------+                      +--------+---------+
                                                    |
                                              TCP / FIX 4.2
                                                    |
                                           +--------v---------+
                                           |   C++ Engine     |
                                           |  (localhost:9001) |
                                           |                  |
                                           |  +------------+  |
                                           |  | OrderBook  |  |
                                           |  | PriceLevel |  |
                                           |  | Matching   |  |
                                           |  | Engine     |  |
                                           |  +------------+  |
                                           +------------------+
```

## Key Design Decisions

### 1. Fixed-Point Prices (`int64_t`, 4 decimal places)

Floating-point arithmetic is non-associative -- `0.1 + 0.2 != 0.3` in IEEE 754,
which causes incorrect order-book bucketing. All prices are stored as integers:
`$189.42 -> 1,894,200` with `PRICE_SCALE = 10,000`.

### 2. Price-Time Priority (FIFO) Matching

Standard exchange matching algorithm:
- **Price priority**: best price matches first (highest bid, lowest ask)
- **Time priority**: at equal prices, earlier orders match first

### 3. O(1) Cancel via Iterator Map

`PriceLevel` maintains a `std::list<Order*>` (stable iterators) plus an
`unordered_map<OrderId, list::iterator>` index. Cancel = one hash lookup +
one `list::erase` = O(1), regardless of queue depth.

This is structurally identical to LRU Cache -- the same pattern of
linked list + hash map for O(1) ordered deletion.

### 4. `std::map` for Price Levels

`std::map<Price, PriceLevel, std::greater<>>` for bids gives `begin() ==
best bid` in O(1). Rejected alternative: `unordered_map` + separate
best-price variable, because ordered iteration is needed for depth snapshots.

### 5. Cache-Line-Aligned Order Struct

`alignas(64)` ensures Order starts on a 64-byte cache line boundary.
Hot fields (price, qty, side) occupy the first cache line; cold fields
(timestamp, status) sit on the second.

### 6. Object Pool Allocator

Eliminates `malloc/new` on the hot path. The pool pre-allocates a flat
array of Order slots and maintains a free-list stack of indices.
`allocate()` and `free()` are O(1) with no syscalls.

Key result: **p99.9 latency spikes reduced from ~1800ns to ~1173ns** by
eliminating non-deterministic allocator jitter.

### 7. epoll Non-Blocking I/O

Single-threaded event loop using Linux `epoll` with edge-triggered (`EPOLLET`)
notifications. `TCP_NODELAY` disables Nagle's algorithm for immediate packet
delivery.

### 8. Branch Prediction Hints

Hot-path branches annotated with `[[likely]]` / `[[unlikely]]` to guide
compiler instruction layout and reduce CPU pipeline flushes.

## Performance

> All measurements on WSL2 Ubuntu 24.04, Intel Core i7 @ 3.54 GHz.
> Two distinct measurement methods are used -- see notes below each table.

---

### In-Process Microbenchmark

**What it measures:** pure matching engine latency with no network or OS
involvement. Orders are allocated and submitted entirely in-process.
Measured with `rdtscp` (CPU timestamp counter, ~1ns resolution).

**How to reproduce:** `./scripts/build.sh bench`

| Operation | Heap p50 | Heap p99 | Heap p99.9 | Pool p50 | Pool p99 | Pool p99.9 | p99 delta |
|-----------|---------|---------|-----------|---------|---------|-----------|-----------|
| Limit order (resting) | 66ns | 141ns | ~1800ns | 64ns | 111ns | ~1173ns | **-21%** |
| Limit order (full fill) | 61ns | 79ns | ~160ns | 60ns | 79ns | ~104ns | ~0% |
| Cancel order | 39ns | 141ns | ~1682ns | 38ns | 103ns | ~252ns | **-27%** |

**Throughput (add + cancel cycle, 500k orders):**

| Allocator | Throughput | Avg latency |
|-----------|-----------|-------------|
| Heap (`new/delete`) | 9.74 M ops/sec | 102ns |
| Object Pool | 13.59 M ops/sec | 73ns |

> **Key insight:** p99.9 improvement matters more than p99 for HFT.
> Heap allocator occasionally calls `mmap/sbrk` (syscall), causing
> sporadic 1000-2000ns spikes. The pool eliminates these entirely.

---

### End-to-End Network Stress Test

**What it measures:** full round-trip latency including TCP stack, FIX
protocol parsing, matching engine, and FIX execution report serialization.
Measured with wall clock (`date +%s%N`) at the sending process boundary.

**How to reproduce:**
```bash
# Start the engine first
./build/Release/bin/hft_server

# Run stress test
./scripts/stress_test.sh 5000 9001
```

| Metric | Value |
|--------|-------|
| Orders per run | 10,000 (5,000 SELL + 5,000 BUY pairs) |
| All orders fully matched | yes (same price, opposite sides) |
| End-to-end throughput | ~50,000-100,000 orders/sec (network-bound) |

> **Key insight:** end-to-end throughput is ~100x lower than in-process
> because TCP adds microseconds of latency per message. Production HFT
> systems use DPDK (kernel-bypass networking) to close this gap.
> The matching engine itself is not the bottleneck.

---

## Project Structure

```
hft/
+-- engine/
|   +-- types.hpp              # Price (int64 fixed-point), Side, OrderType
|   +-- order.hpp              # Cache-line-aligned Order struct (128 bytes)
|   +-- order_book.hpp/cpp     # Dual-sided std::map order book
|   +-- price_level.hpp        # FIFO queue + O(1) cancel
|   +-- matching_engine.hpp/cpp # Price-time priority matching
|   +-- object_pool.hpp        # Fixed-size pool allocator
|   +-- order_id_generator.hpp # Atomic monotonic counter (relaxed ordering)
+-- network/
|   +-- fix_parser.hpp/cpp     # FIX 4.2 parser/serializer (std::from_chars)
|   +-- tcp_server.hpp/cpp     # epoll non-blocking TCP server
+-- frontend/
|   +-- server.js              # Node.js WebSocket bridge
|   +-- src/App.jsx            # React real-time dashboard
+-- tests/                     # Google Test unit tests (72 tests, 100% pass)
+-- bench/                     # rdtscp latency benchmarks
+-- scripts/
|   +-- build.sh               # CMake build wrapper
|   +-- stress_test.sh         # End-to-end network stress test
+-- main.cpp                   # Server entry point
```

## Build & Run

**Requirements:** CMake >= 3.20, GCC >= 13, Node.js >= 18, Linux (epoll required)

```bash
# Build and test
./scripts/build.sh test    # build + run 72 unit tests
./scripts/build.sh bench   # build + run latency benchmark

# Run full system (3 terminals)
./build/Release/bin/hft_server                      # Terminal 1: C++ engine
cd frontend && node server.js                        # Terminal 2: WebSocket bridge
cd frontend && node node_modules/.bin/vite           # Terminal 3: React UI

# Open browser: http://localhost:3000
```

**Send a FIX order directly:**
```bash
printf "8=FIX.4.2\x0135=D\x0111=ORD001\x0155=AAPL\x0154=1\x0138=100\x0144=18942\x0140=2\x0110=000\x01" \
  | nc localhost 9001
```

## Testing

72 unit tests, 100% pass rate.

Key coverage:
- Price-time FIFO ordering verified
- Partial fills and multi-level sweeps
- O(1) cancel correctness (middle-of-queue removal)
- Fixed-point arithmetic: `to_price(0.1) + to_price(0.2) == to_price(0.3)`
- FIX 4.2 parse/serialize round-trip
- Object pool: allocate/free cycle, exhaustion, reuse correctness

## Known Limitations & Future Work

| Area | Current implementation | Production approach |
|------|----------------------|-------------------|
| Network buffer | `std::string` (heap) | Fixed-size ring buffer (zero alloc) |
| Container nodes | System heap | Custom pool allocator for `std::list`/`std::map` |
| Threading model | Single-threaded | Lock-free SPSC queues between pinned cores |
| Network layer | TCP (kernel) | DPDK kernel-bypass |
| Latency measurement | `rdtscp` in-process | Hardware NIC timestamping (PTP) |
| Protocol | Simplified FIX 4.2 | Full FIX + ITCH market data |
| Time source | `steady_clock` (wall) | Injectable clock for deterministic replay |
