#pragma once

#include "engine/matching_engine.hpp"
#include "engine/object_pool.hpp"
#include "network/fix_parser.hpp"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

// Linux-specific headers for epoll and sockets
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>   // TCP_NODELAY
#include <unistd.h>
#include <fcntl.h>

namespace hft {

// ─── TcpServer ───────────────────────────────────────────────────────────────
//
// Single-threaded, non-blocking TCP server using epoll.
//
// WHY epoll over select/poll?
//   select: O(n) scan of all fds on every call, max 1024 fds
//   poll:   O(n) scan, no fd limit but still linear
//   epoll:  O(1) — only returns fds that are ready, scales to 100k+ connections
//
// HOW epoll WORKS:
//   1. Create epoll instance: epoll_create1()
//   2. Register file descriptors: epoll_ctl(EPOLL_CTL_ADD)
//   3. Wait for events: epoll_wait() — blocks until something is ready
//   4. Handle only the ready fds
//
// NON-BLOCKING I/O:
//   All sockets are set to O_NONBLOCK.  read()/write() return immediately
//   even if no data is available (EAGAIN/EWOULDBLOCK).
//   This lets one thread handle many connections without blocking.
//
// TCP_NODELAY:
//   Disables Nagle's algorithm, which buffers small packets to reduce
//   overhead.  In HFT, we want every byte sent immediately, even if small.
//   TCP_NODELAY reduces latency at the cost of slightly higher bandwidth.
//
class TcpServer {
public:
    // Callback types
    using OnTradeCallback = std::function<void(const Trade&, const std::string& symbol)>;

    explicit TcpServer(int port, MatchingEngine& engine);
    ~TcpServer();

    // Non-copyable
    TcpServer(const TcpServer&)            = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    // Start the event loop. Blocks until stop() is called.
    void run();

    // Signal the event loop to exit.
    void stop() noexcept { running_ = false; }

    // Register a callback to be called on each trade.
    void on_trade(OnTradeCallback cb) { trade_cb_ = std::move(cb); }

    int port() const noexcept { return port_; }

private:
    // ── Connection state ──────────────────────────────────────
    struct Connection {
        int         fd;
        std::string recv_buf;   // accumulates partial messages
        std::string send_buf;   // pending outbound data
        uint32_t    seq_num = 0;
    };

    // ── Socket helpers ────────────────────────────────────────
    int  create_listen_socket();
    void set_nonblocking(int fd);
    void set_tcp_nodelay(int fd);
    void accept_connection();
    void close_connection(int fd);

    // ── I/O handlers ─────────────────────────────────────────
    void handle_read(int fd);
    void handle_write(int fd);

    // ── FIX message processing ────────────────────────────────
    // Process one complete FIX message from a connection.
    void process_fix_message(Connection& conn, std::string_view msg);

    // Send a string to a connection (buffered).
    void send_to(Connection& conn, const std::string& data);

    // ── State ─────────────────────────────────────────────────
    int             port_;
    int             epoll_fd_   = -1;
    int             listen_fd_  = -1;
    bool            running_    = false;
    MatchingEngine& engine_;
    OnTradeCallback trade_cb_;

    // Per-connection state, keyed by fd
    std::unordered_map<int, Connection> connections_;

    // Order pool for this server
    static constexpr size_t ORDER_POOL_SIZE = 65'536;
    std::unique_ptr<ObjectPool<Order, ORDER_POOL_SIZE>> order_pool_;

    // Sequence number for outbound messages
    uint32_t out_seq_ = 1;

    // epoll event buffer (reused across epoll_wait calls)
    static constexpr int MAX_EVENTS = 64;
    std::array<epoll_event, MAX_EVENTS> events_;
};

} // namespace hft
