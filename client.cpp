// Multi-Client Chat Client
// Tech: C++17, POSIX sockets, pthreads
// Compile: g++ -std=c++17 -Wall -Wextra -pthread -o client client.cpp
// Usage:   ./client [server_ip]          (default: 127.0.0.1)

#include <atomic>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static const int PORT       = 8080;
static const int MAX_BUFFER = 4096;

static std::atomic<bool> g_running{true};

// ── Receive thread ────────────────────────────────────────────────────────────
// Continuously reads from the socket and prints to stdout.

static void receive_loop(int sock_fd) {
    char buffer[MAX_BUFFER];
    while (g_running) {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            if (g_running) {
                std::cout << "\nServer closed the connection.\n" << std::flush;
                g_running = false;
            }
            break;
        }
        // Print received message; include a prompt hint on a new line
        std::cout << buffer << std::flush;
    }
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    signal(SIGPIPE, SIG_IGN);

    const char* server_ip = (argc > 1) ? argv[1] : "127.0.0.1";

    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(static_cast<uint16_t>(PORT));
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid server address: " << server_ip << "\n";
        close(sock_fd);
        return 1;
    }

    if (connect(sock_fd, reinterpret_cast<sockaddr*>(&server_addr),
                sizeof(server_addr)) < 0) {
        perror("connect");
        close(sock_fd);
        return 1;
    }

    std::cout << "Connected to " << server_ip << ":" << PORT << "\n"
              << "Enter your name: " << std::flush;

    std::string name;
    if (!std::getline(std::cin, name) || name.empty()) {
        std::cerr << "Empty name – disconnecting.\n";
        close(sock_fd);
        return 1;
    }

    // Send name to server
    if (send(sock_fd, name.c_str(), name.size(), MSG_NOSIGNAL) <= 0) {
        perror("send name");
        close(sock_fd);
        return 1;
    }

    // Start background receive thread
    std::thread recv_thread(receive_loop, sock_fd);

    // ── Send loop (main thread) ───────────────────────────────────────────
    std::string line;
    while (g_running && std::getline(std::cin, line)) {
        if (!g_running) break;
        if (line == "/quit") {
            g_running = false;
            break;
        }
        line += "\n";
        ssize_t n = send(sock_fd, line.c_str(), line.size(), MSG_NOSIGNAL);
        if (n <= 0) {
            std::cerr << "Send error – disconnecting.\n";
            g_running = false;
            break;
        }
    }

    g_running = false;
    shutdown(sock_fd, SHUT_RDWR);
    close(sock_fd);

    if (recv_thread.joinable()) recv_thread.join();

    std::cout << "Disconnected.\n";
    return 0;
}
