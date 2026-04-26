#include "engine/matching_engine.hpp"
#include "network/tcp_server.hpp"

#include <cstdio>
#include <csignal>
#include <atomic>

// ─── Signal handling ──────────────────────────────────────────────────────────
static std::atomic<bool> g_shutdown{false};
static hft::TcpServer*   g_server_ptr = nullptr;

static void signal_handler(int) {
    g_shutdown = true;
    if (g_server_ptr) g_server_ptr->stop();
}

// ─── main ────────────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    int port = 9001;
    if (argc > 1) port = std::atoi(argv[1]);

    printf("\n");
    printf("══════════════════════════════════════════\n");
    printf("  HFT Matching Engine Server\n");
    printf("  FIX 4.2 over TCP  |  port %d\n", port);
    printf("══════════════════════════════════════════\n\n");

    // Install signal handler for clean shutdown
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create matching engine for AAPL
    hft::MatchingEngine engine("AAPL");

    // Create TCP server
    hft::TcpServer server(port, engine);
    g_server_ptr = &server;

    // Register trade callback
    server.on_trade([](const hft::Trade& t, const std::string& symbol) {
        printf("[trade] %s: qty=%ld px=%ld aggressor=%lu passive=%lu\n",
               symbol.c_str(), t.quantity, t.price,
               t.aggressor_id, t.passive_id);
    });

    printf("Send FIX 4.2 New Order Single (35=D) to port %d\n", port);
    printf("Example: nc localhost %d\n", port);
    printf("Press Ctrl+C to stop\n\n");

    server.run();

    printf("\nShutdown complete. Total trades: %lu\n",
           engine.book().trade_count());
    return 0;
}
