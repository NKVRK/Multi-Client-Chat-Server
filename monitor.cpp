// Chat Monitor Process
// Reads activity events from the named pipe written by the server and logs
// them to stdout and to a persistent log file.
//
// Tech: C++17, POSIX named pipes (mkfifo)
// Compile: g++ -std=c++17 -Wall -Wextra -pthread -o monitor monitor.cpp
// Usage:   ./monitor [log_file]          (default: chat_monitor.log)

#include <atomic>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char* FIFO_PATH    = "/tmp/chat_monitor_fifo";
static const char* DEFAULT_LOG  = "chat_monitor.log";

static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/) {
    g_running = false;
}

static std::string timestamp() {
    time_t now = time(nullptr);
    char   buf[32] = {};
    struct tm tm_info{};
    localtime_r(&now, &tm_info);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    return std::string(buf);
}

int main(int argc, char* argv[]) {
    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    const char* log_path = (argc > 1) ? argv[1] : DEFAULT_LOG;

    // Create the FIFO if the server hasn't done so yet
    mkfifo(FIFO_PATH, 0666);

    std::ofstream log_file(log_path, std::ios::app);
    if (!log_file.is_open()) {
        std::cerr << "Failed to open log file: " << log_path << "\n";
        return 1;
    }

    std::cout << "Monitor started.\n"
              << "  FIFO : " << FIFO_PATH << "\n"
              << "  Log  : " << log_path  << "\n"
              << "Press Ctrl+C to stop.\n" << std::flush;

    // Open the read end of the FIFO.
    // This call blocks until the server (writer) also opens the FIFO.
    int fifo_fd = open(FIFO_PATH, O_RDONLY);
    if (fifo_fd < 0) {
        perror("open fifo");
        return 1;
    }

    char buffer[4096];
    while (g_running) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t n = read(fifo_fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            std::string entry = "[" + timestamp() + "] " +
                                std::string(buffer, static_cast<size_t>(n));
            // Ensure the entry ends with a newline before writing
            if (entry.back() != '\n') entry += '\n';

            std::cout << entry << std::flush;
            log_file  << entry << std::flush;
        } else if (n == 0) {
            // Writer side closed – wait briefly then try to re-open.
            // This happens if the server restarts.
            close(fifo_fd);
            if (!g_running) break;
            // Re-open blocks until a new server attaches
            fifo_fd = open(FIFO_PATH, O_RDONLY);
            if (fifo_fd < 0) {
                perror("re-open fifo");
                break;
            }
        } else {
            if (g_running) perror("read fifo");
            break;
        }
    }

    close(fifo_fd);
    log_file.close();
    std::cout << "\nMonitor stopped.\n";
    return 0;
}
