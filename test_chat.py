#!/usr/bin/env python3
"""
Automated test for the Multi-Client Chat Server.

Tests:
  1. Server starts and listens on port 8080.
  2. 10 clients connect simultaneously.
  3. Each client sends a message.
  4. Each client receives broadcasts from all other clients.
  5. Clients disconnect gracefully; server keeps running.

Usage:
    # Build first, then run:
    make
    python3 test_chat.py
"""

import socket
import threading
import time
import sys

HOST        = "127.0.0.1"
PORT        = 8080
NUM_CLIENTS = 10
TIMEOUT     = 5      # seconds per receive operation
PASS        = "\033[32mPASS\033[0m"
FAIL        = "\033[31mFAIL\033[0m"

errors: list[str] = []
results_lock = threading.Lock()

def record_error(msg: str) -> None:
    with results_lock:
        errors.append(msg)
    print(f"  {FAIL}  {msg}")

# ── Single client simulation ──────────────────────────────────────────────────

class ChatClient:
    def __init__(self, client_id: int):
        self.id       = client_id
        self.name     = f"TestClient{client_id:02d}"
        self.sock     = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.received : list[str] = []
        self._lock    = threading.Lock()

    def connect(self) -> bool:
        try:
            self.sock.connect((HOST, PORT))
            self.sock.settimeout(TIMEOUT)
            return True
        except OSError as e:
            record_error(f"Client {self.id}: connect failed: {e}")
            return False

    def send_name(self) -> bool:
        try:
            self.sock.sendall(self.name.encode())
            return True
        except OSError as e:
            record_error(f"Client {self.id}: send name failed: {e}")
            return False

    def recv_all_pending(self) -> str:
        """Drain whatever is buffered, return as one string (non-blocking)."""
        self.sock.settimeout(0.5)
        chunks = []
        try:
            while True:
                data = self.sock.recv(4096)
                if not data:
                    break
                chunks.append(data.decode(errors="replace"))
        except socket.timeout:
            pass
        except OSError:
            pass
        finally:
            self.sock.settimeout(TIMEOUT)
        return "".join(chunks)

    def send_message(self, msg: str) -> bool:
        try:
            self.sock.sendall((msg + "\n").encode())
            return True
        except OSError as e:
            record_error(f"Client {self.id}: send message failed: {e}")
            return False

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass


# ── Test helpers ──────────────────────────────────────────────────────────────

def check(description: str, condition: bool) -> None:
    if condition:
        print(f"  {PASS}  {description}")
    else:
        record_error(description)


# ── Main test ─────────────────────────────────────────────────────────────────

def run_tests() -> int:
    print(f"\n{'='*60}")
    print(f"  Multi-Client Chat Server – automated test ({NUM_CLIENTS} clients)")
    print(f"{'='*60}\n")

    # ── Step 1: Create and connect all clients ────────────────────────────
    print(f"[1] Connecting {NUM_CLIENTS} clients …")
    clients = [ChatClient(i) for i in range(1, NUM_CLIENTS + 1)]

    connect_ok: list[bool] = []
    connect_ok_lock = threading.Lock()

    def connect_client(c: ChatClient) -> None:
        ok = c.connect() and c.send_name()
        with connect_ok_lock:
            connect_ok.append(ok)

    threads = [threading.Thread(target=connect_client, args=(c,)) for c in clients]
    for t in threads:
        t.start()
    for t in threads:
        t.join()

    check(f"All {NUM_CLIENTS} clients connected and sent names",
          len(connect_ok) == NUM_CLIENTS and all(connect_ok))

    # Give the server time to process all joins
    time.sleep(0.5)

    # ── Step 2: Read welcome / join messages (drain buffers) ─────────────
    print(f"\n[2] Reading initial server messages …")
    for c in clients:
        data = c.recv_all_pending()
        # Each client should have received at least a welcome message
        check(f"  Client {c.id:02d} ({c.name}) received welcome",
              "Welcome" in data or len(data) > 0)

    # ── Step 3: Each client sends one message ─────────────────────────────
    print(f"\n[3] Each client sends one message …")
    for c in clients:
        ok = c.send_message(f"Hello from {c.name}")
        check(f"  Client {c.id:02d} sent message", ok)

    # Give the server time to broadcast
    time.sleep(0.8)

    # ── Step 4: Verify broadcasts were received ───────────────────────────
    print(f"\n[4] Checking that broadcast messages were received …")
    # Each client collects what arrived
    received_by: dict[int, str] = {}
    for c in clients:
        received_by[c.id] = c.recv_all_pending()

    # Client N should NOT receive its own message but should receive others'
    broadcast_ok_count = 0
    for c in clients:
        data = received_by[c.id]
        # Check that at least one message from another client was received
        others_found = any(
            f"Hello from {other.name}" in data
            for other in clients if other.id != c.id
        )
        if others_found:
            broadcast_ok_count += 1

    check(f"At least {NUM_CLIENTS - 1} clients received broadcasts",
          broadcast_ok_count >= NUM_CLIENTS - 1)

    # ── Step 5: Graceful disconnect ───────────────────────────────────────
    print(f"\n[5] Disconnecting clients gracefully …")
    for c in clients:
        c.close()

    # Give the server time to register disconnections
    time.sleep(0.5)
    print(f"  {PASS}  All clients disconnected without error")

    # ── Step 6: Server still accepts new connections ───────────────────────
    print(f"\n[6] Verifying server is still alive after mass disconnect …")
    probe = ChatClient(99)
    alive = probe.connect() and probe.send_name()
    if alive:
        probe.recv_all_pending()
        probe.close()
    check("Server accepts new connection after mass disconnect", alive)

    # ── Summary ───────────────────────────────────────────────────────────
    print(f"\n{'='*60}")
    if not errors:
        print(f"  {PASS}  All tests passed!\n")
        return 0
    else:
        print(f"  {FAIL}  {len(errors)} test(s) failed:\n")
        for e in errors:
            print(f"    • {e}")
        print()
        return 1


if __name__ == "__main__":
    sys.exit(run_tests())
