"""TCP server for the DS. One DS is served at a time; a new connection that
logs in replaces the previous one."""

import logging
import socket
import threading

from . import protocol

log = logging.getLogger(__name__)

SEND_TIMEOUT = 5    # seconds
IDLE_TIMEOUT = 60   # the DS sends PING every 5 seconds


class Client:
    def __init__(self, sock, address):
        self.sock = sock
        self.address = address
        self.send_lock = threading.Lock()
        self.closed = False

    def send_lines(self, lines):
        data = "".join(f"{text}\n" for text in lines).encode("ascii", "replace")
        with self.send_lock:
            if self.closed:
                return
            try:
                self.sock.sendall(data)
            except OSError as e:
                log.info("Can't send to the DS (%s), dropping it", e)
                self._close()
                return
        for text in lines:
            if text != "PONG":
                log.debug("DS< %s", text)

    def close(self):
        with self.send_lock:
            self._close()

    def _close(self):
        if self.closed:
            return
        self.closed = True
        try:
            self.sock.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        self.sock.close()

    def lines(self):
        """Yields received lines until the DS disconnects or goes quiet."""
        buffer = b""
        idle = 0
        while not self.closed:
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                idle += SEND_TIMEOUT
                if idle >= IDLE_TIMEOUT:
                    log.info("No data from the DS for %d s", IDLE_TIMEOUT)
                    return
                continue
            except OSError:
                return
            if not chunk:
                return
            idle = 0
            buffer += chunk
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                yield raw.decode("ascii", "replace").strip()
            if len(buffer) > protocol.MAX_LINE:
                log.warning("Line from the DS is too long, disconnecting")
                return


class Server:
    def __init__(self, bridge, host, port):
        self.bridge = bridge
        self.host = host
        self.port = port
        self.ready = threading.Event()

    def serve_forever(self):
        with socket.create_server((self.host, self.port)) as listener:
            listener.settimeout(1)
            self.port = listener.getsockname()[1]
            log.info("Waiting for the DS on %s:%d", self.host, self.port)
            self.ready.set()
            while not self.bridge.stopping.is_set():
                try:
                    sock, address = listener.accept()
                except socket.timeout:
                    continue
                threading.Thread(
                    target=self._serve, args=(sock, address), name=f"ds-{address[0]}", daemon=True
                ).start()

    def _serve(self, sock, address):
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        sock.settimeout(SEND_TIMEOUT)
        client = Client(sock, address)
        log.info("DS connected from %s", address[0])

        logged_in = False
        try:
            for raw in client.lines():
                command, args = protocol.parse(raw)
                if command is None:
                    continue
                if command != "PING":
                    log.debug("DS> %s", "HELLO ..." if command == "HELLO" else raw)

                if not logged_in:
                    if command == "HELLO" and self.bridge.check_hello(args):
                        logged_in = True
                        self.bridge.attach(client)
                    else:
                        reason = "Wrong secret" if command == "HELLO" else "Send HELLO first"
                        log.warning("Refusing DS at %s: %s", address[0], reason)
                        client.send_lines([protocol.line("DENIED", text=reason)])
                        break
                    continue

                self.bridge.handle(command, args)
        except Exception:
            log.exception("Error while serving the DS")
        finally:
            self.bridge.detach(client)
            client.close()
            log.info("DS at %s disconnected", address[0])
