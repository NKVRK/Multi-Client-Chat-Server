# Multi-Client Chat Server

A terminal-based multi-client TCP chat server built in pure C++17 with POSIX
APIs — no external libraries required.

## Features

| Requirement | Implementation |
|---|---|
| Multiple simultaneous clients | One `std::thread` per client, detached |
| Broadcast messages | `broadcast()` iterates the shared client list |
| Mutex-protected shared data | `std::mutex` guards the client list and monitor pipe |
| Named-pipe IPC (monitor process) | `mkfifo` + `/tmp/chat_monitor_fifo` |
| Graceful disconnect | `recv` ≤ 0 triggers clean removal and broadcast |
| CLI client | Separate send (main) and receive threads |
| ≥ 10 simultaneous connections | Tested with automated Python test script |

## Tech Stack

- **Language:** C++17  
- **Networking:** POSIX sockets (`socket`, `bind`, `listen`, `accept`, `recv`, `send`)  
- **Concurrency:** `std::thread` + `std::mutex` (backed by pthreads)  
- **IPC:** Named pipe created with `mkfifo(3)`  
- **Build:** `g++` on Linux, no external libraries  

## Files

| File | Description |
|---|---|
| `server.cpp` | TCP chat server (accepts connections, broadcasts, writes to FIFO) |
| `client.cpp` | CLI chat client (separate send/receive threads) |
| `monitor.cpp` | Monitor process (reads FIFO, logs timestamped events to file) |
| `Makefile` | Builds `server`, `client`, and `monitor` |
| `test_chat.py` | Automated test — 10 simultaneous clients |

## Build

```bash
make          # builds server, client, monitor
make clean    # removes binaries and log file
```

Requires `g++` with C++17 support and POSIX headers (standard on Linux).

## Running

### 1. Start the monitor (optional — receives IPC events from server)

```bash
./monitor
# or with a custom log file:
./monitor mylog.txt
```

### 2. Start the server

```bash
./server
# Listens on TCP port 8080
# Writes activity to /tmp/chat_monitor_fifo (read by the monitor)
```

### 3. Connect clients

```bash
./client                  # connects to 127.0.0.1:8080
./client 192.168.1.10     # connect to a remote server
```

Each client is prompted for a display name, then can type messages that are
broadcast to all other connected clients. Type `/quit` to disconnect.

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                       server                             │
│                                                          │
│  main thread: accept() loop                              │
│    └─ spawns one std::thread per client                  │
│         • recv messages from client                      │
│         • broadcast to all other clients (mutex guarded) │
│         • write events to named pipe (mutex guarded)     │
└──────────────────┬───────────────────────────────────────┘
                   │ named pipe /tmp/chat_monitor_fifo
                   ▼
┌──────────────────────────────────────────────────────────┐
│                      monitor                             │
│  reads events from FIFO, timestamps them, writes to      │
│  stdout and to chat_monitor.log                          │
└──────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────┐
│                       client                             │
│  main thread: reads stdin → sends to server              │
│  recv thread: reads from server → prints to stdout       │
└──────────────────────────────────────────────────────────┘
```

## Automated Test

```bash
# Build, start server, run test, stop server
make
./server &
python3 test_chat.py
kill %1
```

The test:
1. Connects 10 clients simultaneously  
2. Verifies each receives a welcome message  
3. Has each client broadcast a message  
4. Verifies broadcasts are received by other clients  
5. Disconnects all clients gracefully  
6. Confirms the server remains alive after mass disconnect  
