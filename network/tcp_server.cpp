#include "network/tcp_server.hpp"

#include <cstring>
#include <stdexcept>
#include <cstdio>
#include <cerrno>

namespace hft {

// ─── Constructor / Destructor ─────────────────────────────────────────────────

TcpServer::TcpServer(int port, MatchingEngine& engine)
    : port_(port), engine_(engine)
{
    order_pool_ = std::make_unique<ObjectPool<Order, ORDER_POOL_SIZE>>();
    // Create epoll instance
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0)
        throw std::runtime_error("epoll_create1 failed: " +
                                  std::string(strerror(errno)));

    // Create and configure listen socket
    listen_fd_ = create_listen_socket();

    // Register listen socket with epoll
    epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0)
        throw std::runtime_error("epoll_ctl failed");
}

TcpServer::~TcpServer() {
    for (auto& [fd, _] : connections_) ::close(fd);
    if (listen_fd_ >= 0) ::close(listen_fd_);
    if (epoll_fd_  >= 0) ::close(epoll_fd_);
}

// ─── Socket helpers ───────────────────────────────────────────────────────────

int TcpServer::create_listen_socket() {
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        throw std::runtime_error("socket() failed");

    // Allow reuse of port immediately after process restart
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(port_));

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed on port " +
                                  std::to_string(port_) + ": " +
                                  strerror(errno));

    if (listen(fd, 128) < 0)
        throw std::runtime_error("listen() failed");

    printf("[server] Listening on port %d\n", port_);
    return fd;
}

void TcpServer::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void TcpServer::set_tcp_nodelay(int fd) {
    // Disable Nagle's algorithm — send packets immediately without buffering.
    // Critical for low latency: without this, small messages may be delayed
    // up to 200ms waiting for more data to batch.
    int opt = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
}

// ─── Connection management ────────────────────────────────────────────────────

void TcpServer::accept_connection() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t   addr_len = sizeof(client_addr);
        int client_fd = accept4(listen_fd_,
                                reinterpret_cast<sockaddr*>(&client_addr),
                                &addr_len,
                                SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break; // no more pending
            break;
        }

        set_tcp_nodelay(client_fd);

        // Register client with epoll for read events
        // EPOLLET = edge-triggered: only notify on state change, not repeatedly
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.fd = client_fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &ev);

        connections_.emplace(client_fd, Connection{client_fd});
        printf("[server] Client connected fd=%d\n", client_fd);
    }
}

void TcpServer::close_connection(int fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    connections_.erase(fd);
    printf("[server] Client disconnected fd=%d\n", fd);
}

// ─── I/O handlers ────────────────────────────────────────────────────────────

void TcpServer::handle_read(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    Connection& conn = it->second;

    char buf[4096];
    while (true) {
        ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            conn.recv_buf.append(buf, static_cast<size_t>(n));

            // Process complete FIX messages.
            // FIX messages end with tag 10 (checksum) followed by SOH.
            // We scan for the pattern "10=xxx\x01".
            while (true) {
                // Look for end-of-message marker
                size_t end = conn.recv_buf.find("\x01" "10=");
                if (end == std::string::npos) break;

                // Find the SOH after the checksum value
                size_t cs_end = conn.recv_buf.find('\x01', end + 1);
                if (cs_end == std::string::npos) break;

                // Extract and process complete message
                std::string msg = conn.recv_buf.substr(0, cs_end + 1);
                conn.recv_buf.erase(0, cs_end + 1);
                process_fix_message(conn, msg);
            }
        } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            close_connection(fd);
            return;
        } else {
            break;  // EAGAIN — no more data right now
        }
    }
}

void TcpServer::handle_write(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    Connection& conn = it->second;

    while (!conn.send_buf.empty()) {
        ssize_t n = ::write(fd, conn.send_buf.data(), conn.send_buf.size());
        if (n > 0) {
            conn.send_buf.erase(0, static_cast<size_t>(n));
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;  // socket buffer full, try again later
        } else {
            close_connection(fd);
            return;
        }
    }

    // If send buffer is empty, stop watching for write events
    if (conn.send_buf.empty()) {
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLET | EPOLLRDHUP;
        ev.data.fd = fd;
        epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
    }
}

void TcpServer::send_to(Connection& conn, const std::string& data) {
    conn.send_buf += data;

    // Register for write events so we flush when socket is ready
    epoll_event ev{};
    ev.events  = EPOLLIN | EPOLLOUT | EPOLLET | EPOLLRDHUP;
    ev.data.fd = conn.fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn.fd, &ev);
}

// ─── FIX message processing ───────────────────────────────────────────────────

void TcpServer::process_fix_message(Connection& conn, std::string_view raw) {
    auto msg = fix::FIXParser::parse(raw);

    if (!msg.valid) {
        printf("[fix] Parse error: %s\n", msg.error.c_str());
        return;
    }

    if (msg.msg_type == fix::MSG_NEW_ORDER) {
        // Allocate Order from pool
        Order* order = nullptr;
        if (msg.ord_type == OrderType::Market) {
            order = order_pool_->make(
                Order::make_market(
                    static_cast<OrderId>(++out_seq_),
                    msg.side,
                    msg.order_qty));
        } else {
            order = order_pool_->make(
                Order::make_limit(
                    static_cast<OrderId>(++out_seq_),
                    msg.side,
                    msg.price,
                    msg.order_qty));
        }

        if (!order) {
            printf("[fix] Order pool exhausted\n");
            return;
        }

        // Submit to matching engine
        auto result = engine_.process_order(order);

        // Send execution reports for each fill
        for (const auto& trade : result.trades) {
            auto exec_rpt = fix::FIXParser::make_exec_report(
                msg.clord_id,
                msg.symbol,
                msg.side,
                trade.price,
                trade.quantity,
                order->remaining_qty,
                ++out_seq_);
            send_to(conn, exec_rpt);

            // Fire trade callback (e.g. for WebSocket broadcast)
            if (trade_cb_) trade_cb_(trade, msg.symbol);

            printf("[match] Fill: %s %ld @ %ld (order %s)\n",
                   msg.symbol.c_str(), trade.quantity,
                   trade.price, msg.clord_id.c_str());
        }

        // If order rested on book, don't free it — engine holds the pointer.
        // If order was fully filled or cancelled, return to pool.
        if (!result.resting) {
            order_pool_->destroy(order);
        }

    } else if (msg.msg_type == fix::MSG_CANCEL) {
        // For cancel we'd need to look up the order by clord_id.
        // Simplified: use clord_id as order_id directly.
        OrderId id = 0;
        std::from_chars(msg.clord_id.data(),
                        msg.clord_id.data() + msg.clord_id.size(), id);
        bool cancelled = engine_.cancel_order(id);
        printf("[fix] Cancel order %s: %s\n",
               msg.clord_id.c_str(), cancelled ? "OK" : "NOT FOUND");
    }
}

// ─── Event loop ───────────────────────────────────────────────────────────────

void TcpServer::run() {
    running_ = true;
    printf("[server] Event loop started\n");

    while (running_) {
        // Wait up to 100ms for events
        int nfds = epoll_wait(epoll_fd_, events_.data(), MAX_EVENTS, 100);

        for (int i = 0; i < nfds; ++i) {
            int fd = events_[i].data.fd;

            if (fd == listen_fd_) {
                // New incoming connection
                accept_connection();
            } else if (events_[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                // Client disconnected or error
                close_connection(fd);
            } else {
                if (events_[i].events & EPOLLIN)  handle_read(fd);
                if (events_[i].events & EPOLLOUT) handle_write(fd);
            }
        }
    }

    printf("[server] Event loop stopped\n");
}

} // namespace hft
