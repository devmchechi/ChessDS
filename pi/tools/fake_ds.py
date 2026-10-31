#!/usr/bin/env python3
"""Pretend to be the Nintendo DS, to test the bridge from a terminal.

    python tools/fake_ds.py --host 127.0.0.1 --secret changeme

Type protocol commands (see docs/PROTOCOL.md), for example:
    AI 1 10 0 random      start a game against Stockfish level 1
    MOVE e2e4             play a move
    RESIGN
The board is printed whenever the bridge sends a new position.
"""

import argparse
import os
import socket
import threading
import time

PROTOCOL_VERSION = 1


def print_board(placement):
    for rank, row in zip(range(8, 0, -1), placement.split("/")):
        squares = "".join("." * int(c) if c.isdigit() else c for c in row)
        print(f"   {rank} {' '.join(squares)}")
    print("     a b c d e f g h")


def receive(sock):
    buffer = b""
    while True:
        try:
            data = sock.recv(4096)
        except OSError:
            data = b""
        if not data:
            print("\n[connection closed]")
            os._exit(0)
        buffer += data
        while b"\n" in buffer:
            raw, buffer = buffer.split(b"\n", 1)
            text = raw.decode("ascii", "replace")
            if text == "PONG":
                continue
            if text.startswith("LEGAL ") and len(text) > 70:
                text = text[:70] + " ..."
            print(f"< {text}")
            if text.startswith("STATE "):
                print_board(text.split()[1])


def keep_alive(sock):
    while True:
        time.sleep(5)
        try:
            sock.sendall(b"PING\n")
        except OSError:
            return


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5555)
    parser.add_argument("--secret", default="changeme")
    args = parser.parse_args()

    sock = socket.create_connection((args.host, args.port), timeout=10)
    sock.settimeout(None)
    sock.sendall(f"HELLO {PROTOCOL_VERSION} {args.secret}\n".encode())
    threading.Thread(target=receive, args=(sock,), daemon=True).start()
    threading.Thread(target=keep_alive, args=(sock,), daemon=True).start()

    try:
        while True:
            command = input().strip()
            if command:
                sock.sendall(command.encode("ascii", "replace") + b"\n")
    except (EOFError, KeyboardInterrupt):
        pass
    finally:
        sock.close()


if __name__ == "__main__":
    main()
