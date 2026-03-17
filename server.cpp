// Multi-Client TCP Chat Server
// Tech: C++17, POSIX sockets, pthreads, named pipes (mkfifo)
// Compile: g++ -std=c++17 -Wall -Wextra -pthread -o server server.cpp

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const int    PORT        = 8080;
static const int    MAX_BUFFER  = 4096;
static const char*  FIFO_PATH   = "/tmp/chat_monitor_fifo";

// ── Client record ────────────────────────────────────────────────────────────

struct ClientInfo {
    int         fd;
    std::string name;
    std::string ip;
};

// ── Shared state ─────────────────────────────────────────────────────────────

static std::vector<ClientInfo> g_clients;
static std::mutex              g_clients_mutex;

static int                     g_monitor_fd = -1;
static std::mutex              g_monitor_mutex;

static std::atomic<bool>       g_running{true};
static int                     g_server_fd = -1;

// ── Named-pipe (IPC) helpers ─────────────────────────────────────────────────

// Try to open the write end of the FIFO without blocking.
// O_WRONLY | O_NONBLOCK returns ENXIO if no reader is attached; we treat that
// as "monitor not running yet" and silently skip logging until it is.
static void monitor_try_open() {
    if (g_monitor_fd < 0) {
        g_monitor_fd = open(FIFO_PATH, O_WRONLY | O_NONBLOCK);
        // ENXIO is expected when no monitor process has opened the read end yet
        if (g_monitor_fd < 0 && errno != ENXIO) {
            perror("open monitor fifo");
        }
    }
}

static void log_to_monitor(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_monitor_mutex);
    monitor_try_open();
    if (g_monitor_fd < 0) return;

    std::string line = message + "\n";
    ssize_t n = write(g_monitor_fd, line.c_str(), line.size());
    if (n < 0) {
        close(g_monitor_fd);
        g_monitor_fd = -1;   // will retry next call
    }
}

// ── Broadcast ────────────────────────────────────────────────────────────────

static void broadcast(const std::string& message, int sender_fd) {
    std::lock_guard<std::mutex> lock(g_clients_mutex);
    for (const auto& c : g_clients) {
        if (c.fd != sender_fd) {
            // MSG_NOSIGNAL: don't raise SIGPIPE if the peer has closed
            send(c.fd, message.c_str(), message.size(), MSG_NOSIGNAL);
        }
    }
}

// ── Per-client thread ────────────────────────────────────────────────────────

static void handle_client(int client_fd, std::string client_ip) {
    char buffer[MAX_BUFFER];

    // ── 1. Receive client name ────────────────────────────────────────────
    memset(buffer, 0, sizeof(buffer));
    int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        close(client_fd);
        return;
    }

    std::string name(buffer, static_cast<size_t>(bytes));
    // Strip trailing whitespace / newlines
    size_t end = name.find_last_not_of(" \n\r\t");
    name = (end == std::string::npos) ? "unknown" : name.substr(0, end + 1);
    if (name.empty()) name = "unknown";

    // ── 2. Register client ────────────────────────────────────────────────
    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        g_clients.push_back({client_fd, name, client_ip});
    }

    std::string join_msg = "[" + name + " has joined the chat]\n";
    std::cout << join_msg << std::flush;
    log_to_monitor("JOIN " + name + " from " + client_ip);
    broadcast(join_msg, client_fd);

    // Send welcome back to this client only
    std::string welcome = "Welcome, " + name + "! Type messages or /quit to exit.\n";
    send(client_fd, welcome.c_str(), welcome.size(), MSG_NOSIGNAL);

    // ── 3. Message loop ───────────────────────────────────────────────────
    while (g_running) {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;   // disconnect or error

        std::string msg(buffer, static_cast<size_t>(n));
        // Strip trailing whitespace
        size_t e = msg.find_last_not_of(" \n\r\t");
        if (e == std::string::npos) continue;
        msg = msg.substr(0, e + 1);
        if (msg.empty()) continue;

        std::string full_msg = "[" + name + "]: " + msg + "\n";
        std::cout << full_msg << std::flush;
        log_to_monitor("MSG " + name + ": " + msg);
        broadcast(full_msg, client_fd);
    }

    // ── 4. Deregister & announce departure ───────────────────────────────
    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        g_clients.erase(
            std::remove_if(g_clients.begin(), g_clients.end(),
                           [client_fd](const ClientInfo& c) {
                               return c.fd == client_fd;
                           }),
            g_clients.end());
    }

    std::string leave_msg = "[" + name + " has left the chat]\n";
    std::cout << leave_msg << std::flush;
    log_to_monitor("LEAVE " + name);
    broadcast(leave_msg, client_fd);

    close(client_fd);
}

// ── Signal handler ────────────────────────────────────────────────────────────

static void signal_handler(int /*sig*/) {
    g_running = false;
    // Wake the blocking accept() by closing the listening socket
    if (g_server_fd >= 0) {
        shutdown(g_server_fd, SHUT_RDWR);
        close(g_server_fd);
        g_server_fd = -1;
    }
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    // Ignore SIGPIPE – handle broken-pipe errors via return values instead
    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    // Create the named pipe used for IPC with the monitor process
    mkfifo(FIFO_PATH, 0666);
    monitor_try_open();

    // Create TCP socket
    g_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(g_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<uint16_t>(PORT));

    if (bind(g_server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(g_server_fd, 32) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "Chat server listening on port " << PORT << "\n"
              << "Monitor FIFO: " << FIFO_PATH << "\n"
              << "Press Ctrl+C to stop.\n" << std::flush;
    log_to_monitor("SERVER_START port=" + std::to_string(PORT));

    // ── Accept loop ───────────────────────────────────────────────────────
    while (g_running) {
        sockaddr_in client_addr{};
        socklen_t   client_len = sizeof(client_addr);

        int client_fd = accept(g_server_fd,
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &client_len);
        if (client_fd < 0) {
            if (g_running) perror("accept");
            continue;
        }

        char ip_buf[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
        std::string ip(ip_buf);

        std::cout << "New connection from " << ip << "\n" << std::flush;
        log_to_monitor("CONNECT " + ip);

        // Detach a thread per client – the thread owns the socket lifetime
        std::thread(handle_client, client_fd, ip).detach();
    }

    // ── Shutdown ──────────────────────────────────────────────────────────
    {
        std::lock_guard<std::mutex> lock(g_clients_mutex);
        for (auto& c : g_clients) close(c.fd);
        g_clients.clear();
    }
    log_to_monitor("SERVER_STOP");
    {
        std::lock_guard<std::mutex> lock(g_monitor_mutex);
        if (g_monitor_fd >= 0) {
            close(g_monitor_fd);
            g_monitor_fd = -1;
        }
    }

    std::cout << "\nServer stopped.\n";
    return 0;
}
