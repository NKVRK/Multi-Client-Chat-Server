# Multi-Client Chat Server

![Language](https://img.shields.io/badge/language-C%2B%2B17-blue?logo=cplusplus)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey?logo=linux)
![Build](https://img.shields.io/badge/build-make-brightgreen)
![License](https://img.shields.io/badge/license-MIT-green)
![Connections](https://img.shields.io/badge/tested-10%2B%20simultaneous%20clients-informational)

A terminal-based, multi-client TCP chat server written in **pure C++17** using only POSIX APIs — no external libraries required. Supports unlimited simultaneous clients, real-time message broadcasting, and an optional out-of-process monitor that logs server activity via a named pipe (IPC).

---

## Table of Contents

- [Features](#features)
- [Project Structure](#project-structure)
- [Tech Stack](#tech-stack)
- [Prerequisites](#prerequisites)
- [Build](#build)
- [Usage](#usage)
  - [1. Start the Monitor (optional)](#1-start-the-monitor-optional)
  - [2. Start the Server](#2-start-the-server)
  - [3. Connect Clients](#3-connect-clients)
- [Architecture](#architecture)
- [Automated Tests](#automated-tests)
- [Contributing](#contributing)
- [License](#license)

---

## Features

| Feature | Details |
|---|---|
| Multiple simultaneous clients | One `std::thread` per client, detached |
| Real-time broadcast | `broadcast()` iterates the shared client list under a mutex |
| Thread-safe shared state | `std::mutex` guards the client list and the monitor pipe |
| Named-pipe IPC | `mkfifo` creates `/tmp/chat_monitor_fifo`; monitor reads it independently |
| Graceful disconnect | `recv` ≤ 0 triggers clean removal and departure broadcast |
| Dual-threaded CLI client | Separate send (main thread) and receive (background thread) |
| Automated integration test | Python script connects 10 clients simultaneously and validates broadcasts |
| Zero external dependencies | Standard C++17 + POSIX only |

---

## Project Structure

```
Multi-Client-Chat-Server/
├── server.cpp        # TCP chat server — accepts connections, broadcasts messages, writes to FIFO
├── client.cpp        # CLI chat client — dual-threaded send/receive
├── monitor.cpp       # Monitor process — reads FIFO, timestamps and logs events
├── Makefile          # Builds server, client, and monitor
└── test_chat.py      # Automated integration test (Python 3)
```

---

## Tech Stack

| Layer | Technology |
|---|---|
| Language | C++17 |
| Networking | POSIX sockets (`socket`, `bind`, `listen`, `accept`, `recv`, `send`) |
| Concurrency | `std::thread` + `std::mutex` (backed by pthreads) |
| IPC | Named pipe via `mkfifo(3)` at `/tmp/chat_monitor_fifo` |
| Build system | GNU Make + `g++` |

---

## Prerequisites

- **Linux** (or any POSIX-compatible OS)
- **g++** ≥ 7 with C++17 support (`-std=c++17`)
- **Python 3** (only needed to run the automated test)

Check your compiler version:

```bash
g++ --version
```

---

## Build

```bash
make          # compile server, client, and monitor
make clean    # remove binaries and chat_monitor.log
```

Each binary is compiled with `-Wall -Wextra -pthread`.

---

## Usage

### 1. Start the Monitor (optional)

The monitor is an independent process that reads server events from the named pipe and writes timestamped entries to both stdout and a log file.

```bash
./monitor                  # logs to chat_monitor.log (default)
./monitor /var/log/chat.log  # custom log file path
```

> **Note:** The monitor can be started before or after the server. The named pipe `/tmp/chat_monitor_fifo` is created automatically by whichever process runs first.

### 2. Start the Server

```bash
./server
# Listening on TCP port 8080
# Broadcasting via /tmp/chat_monitor_fifo
# Press Ctrl+C to stop gracefully
```

### 3. Connect Clients

```bash
./client                   # connect to localhost (127.0.0.1:8080)
./client 192.168.1.10      # connect to a remote server
```

Each client is prompted for a **display name**. Once connected, messages typed in one client are broadcast to all other connected clients. Type `/quit` to disconnect cleanly.

**Example session:**

```
Connected to 127.0.0.1:8080
Enter your name: Alice
Welcome, Alice! Type messages or /quit to exit.
[Bob has joined the chat]
[Bob]: Hey everyone!
Hello Bob!
[Bob]: Nice to meet you, Alice!
/quit
Disconnected.
```

---

## Architecture

```
 ┌──────────────────────────────────────────────────────────┐
 │                        server                            │
 │                                                          │
 │   main thread ── accept() loop                           │
 │       └── spawns one std::thread per connected client    │
 │               • recv messages from client                │
 │               • broadcast to all peers (mutex guarded)   │
 │               • write events to named pipe (mutex guard) │
 └──────────────────────┬───────────────────────────────────┘
                        │  named pipe: /tmp/chat_monitor_fifo
                        ▼
 ┌──────────────────────────────────────────────────────────┐
 │                       monitor                            │
 │   reads events from FIFO → timestamps them →             │
 │   writes to stdout + chat_monitor.log                    │
 └──────────────────────────────────────────────────────────┘

 ┌──────────────────────────────────────────────────────────┐
 │                       client                             │
 │   main thread  : reads stdin → sends messages to server  │
 │   recv thread  : reads from server → prints to stdout    │
 └──────────────────────────────────────────────────────────┘
```

**Key design decisions:**

- **One thread per client** keeps the code simple and avoids the complexity of `select`/`epoll` while still supporting concurrent clients.
- **`MSG_NOSIGNAL`** on every `send` prevents `SIGPIPE` from crashing the server when a peer disconnects mid-write.
- **Non-blocking FIFO open** (`O_NONBLOCK`) means the server starts immediately even if no monitor is running; it retries on each log attempt.
- **Monitor reconnect** — if the server restarts, the monitor re-opens the read end of the FIFO automatically.

---

## Automated Tests

The `test_chat.py` script is a self-contained integration test that exercises the full client/server/broadcast flow.

```bash
make               # build all binaries
./server &         # start server in the background
python3 test_chat.py
kill %1            # stop the server
```

**What the test validates:**

1. All 10 clients connect simultaneously and send their names
2. Each client receives a welcome message from the server
3. Each client sends a broadcast message
4. Every other client receives that broadcast
5. All clients disconnect gracefully without errors
6. The server remains alive and accepts new connections after mass disconnect

A successful run produces a coloured summary:

```
============================================================
  Multi-Client Chat Server – automated test (10 clients)
============================================================

[1] Connecting 10 clients …
  PASS  All 10 clients connected and sent names
...
  PASS  All tests passed!
```

---

## Contributing

Contributions are welcome! Please follow these steps:

1. **Fork** the repository and create a feature branch:
   ```bash
   git checkout -b feature/your-feature-name
   ```
2. Make your changes, keeping the zero-dependency and C++17 constraints in mind.
3. Ensure the project still builds cleanly:
   ```bash
   make clean && make
   ```
4. Run the automated test to confirm nothing is broken:
   ```bash
   ./server & python3 test_chat.py; kill %1
   ```
5. **Open a Pull Request** with a clear description of what you changed and why.

Please keep each PR focused on a single concern. Bug reports and feature requests are also welcome via [GitHub Issues](../../issues).

---

## License

This project is released under the [MIT License](LICENSE).

```
MIT License

Copyright (c) 2024 NKVRK

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
