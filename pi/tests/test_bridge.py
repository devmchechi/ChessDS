"""End-to-end tests: a real Server and Bridge, a fake Lichess, and a socket
playing the part of the DS."""

import queue
import socket
import threading
import time
import unittest

from chessds_bridge import bridge as bridge_module
from chessds_bridge.bridge import Bridge
from chessds_bridge.lichess import LichessError
from chessds_bridge.server import Server

SECRET = "s3cret"


class FakeStream:
    def __init__(self):
        self.items = queue.Queue()
        self.closed = threading.Event()

    def push(self, item):
        self.items.put(item)

    def end(self):
        self.items.put(StopIteration)

    def __iter__(self):
        while True:
            if self.closed.is_set():
                raise ConnectionError("stream closed")
            try:
                item = self.items.get(timeout=0.05)
            except queue.Empty:
                continue
            if item is StopIteration:
                return
            yield item

    def close(self):
        self.closed.set()

    def __enter__(self):
        return self

    def __exit__(self, *exc_info):
        self.close()


class FakeLichess:
    def __init__(self):
        self.events = FakeStream()
        self.games = {}
        self.seeks = []
        self.challenges = []
        self.calls = []
        self.move_error = None

    def game(self, game_id):
        return self.games.setdefault(game_id, FakeStream())

    def account(self):
        return {"id": "dsplayer", "username": "DSPlayer"}

    def stream_events(self):
        return self.events

    def stream_game(self, game_id):
        return self.game(game_id)

    def seek(self, minutes, increment, rated):
        self.calls.append(("seek", minutes, increment, rated))
        stream = FakeStream()
        self.seeks.append(stream)
        return stream

    def challenge(self, username, limit, increment, rated):
        self.calls.append(("challenge", username, limit, increment, rated))
        stream = FakeStream()
        self.challenges.append(stream)
        return stream

    def challenge_ai(self, level, limit, increment, color):
        self.calls.append(("ai", level, limit, increment, color))
        return {"id": "aigame01"}

    def __getattr__(self, name):
        # move, resign, abort, draw, accept_challenge, ... just get recorded.
        def record(*args):
            if name == "move" and self.move_error:
                raise self.move_error
            self.calls.append((name, *args))
        return record


def game_full(game_id, moves="", status="started"):
    return {
        "type": "gameFull",
        "id": game_id,
        "rated": False,
        "clock": {"initial": 600000, "increment": 0},
        "white": {"id": "dsplayer", "name": "DSPlayer", "rating": 1500},
        "black": {"id": "rival", "name": "Rival", "rating": 1600},
        "initialFen": "startpos",
        "state": {"type": "gameState", "moves": moves, "wtime": 600000, "btime": 600000,
                  "status": status},
    }


class BridgeTest(unittest.TestCase):
    def setUp(self):
        bridge_module.GAME_START_WAIT = 0.5
        self.lichess = FakeLichess()
        self.bridge = Bridge(self.lichess, SECRET, friends=["alice", "bad name!"])
        self.bridge.start()
        self.server = Server(self.bridge, "127.0.0.1", 0)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.assertTrue(self.server.ready.wait(5))
        self.sockets = []

    def tearDown(self):
        self.bridge.stop()
        for sock in self.sockets:
            sock.close()

    def connect(self, secret=SECRET):
        sock = socket.create_connection(("127.0.0.1", self.server.port), timeout=5)
        self.sockets.append(sock)
        ds = DS(sock)
        ds.send(f"HELLO 1 {secret}")
        return ds

    def logged_in(self):
        ds = self.connect()
        self.assertEqual(ds.expect("WELCOME"), "WELCOME DSPlayer 1")
        ds.expect("READY")
        return ds

    def start_game(self, ds, game_id="game0001"):
        self.lichess.events.push({"type": "gameStart",
                                  "game": {"gameId": game_id, "speed": "rapid",
                                           "compat": {"board": True}}})
        self.lichess.game(game_id).push(game_full(game_id))
        ds.expect("GAME")
        ds.expect("STATE")
        return ds.expect("LEGAL")

    def test_wrong_secret_is_denied(self):
        ds = self.connect(secret="nope")
        self.assertTrue(ds.expect("DENIED").startswith("DENIED Wrong secret"))

    def test_sync_after_login(self):
        ds = self.connect()
        self.assertEqual(ds.expect("WELCOME"), "WELCOME DSPlayer 1")
        self.assertEqual(ds.expect("FRIENDS"), "FRIENDS alice")
        ds.expect("READY")
        ds.send("PING")
        ds.expect("PONG")

    def test_play_a_game(self):
        ds = self.logged_in()
        legal = self.start_game(ds)
        self.assertEqual(len(legal.split()), 21)

        ds.send("MOVE e2e4")
        wait_for(lambda: ("move", "game0001", "e2e4") in self.lichess.calls)

        stream = self.lichess.game("game0001")
        stream.push({"type": "gameState", "moves": "e2e4 e7e5", "wtime": 599000,
                     "btime": 598000, "status": "started"})
        state = ds.expect("STATE")
        self.assertIn(" w e7e5 599000 598000 2 0 -", state)
        ds.expect("LEGAL")

        stream.push({"type": "chatLine", "room": "player", "username": "Rival", "text": "hi!"})
        self.assertEqual(ds.expect("CHAT"), "CHAT Rival hi!")

        stream.push({"type": "opponentGone", "gone": True, "claimWinInSeconds": 10})
        self.assertIn(ds.expect("GONE"), ("GONE 10", "GONE 9"))

        stream.push({"type": "gameState", "moves": "e2e4 e7e5", "status": "resign",
                     "winner": "white"})
        self.assertEqual(ds.expect("END"), "END resign w Black resigned - White wins")

        ds.send("LEAVE")
        ds.send("PING")
        ds.expect("PONG")
        self.assertIsNone(self.bridge.game)

    def test_illegal_move_resends_the_position(self):
        ds = self.logged_in()
        self.start_game(ds)
        ds.send("MOVE e2e5")
        state = ds.expect("STATE")
        self.assertIn("PPPPPPPP", state)
        self.assertEqual(ds.expect("ERROR"), "ERROR Illegal move e2e5")

    def test_lichess_refusing_a_move_resends_the_position(self):
        ds = self.logged_in()
        self.start_game(ds)
        self.lichess.move_error = LichessError(400, "Not your turn, or game already over")
        ds.send("MOVE e2e4")
        ds.expect("STATE")
        self.assertEqual(ds.expect("ERROR"), "ERROR Not your turn, or game already over")

    def test_reconnecting_ds_gets_the_game(self):
        ds = self.logged_in()
        self.start_game(ds)
        ds.close()
        ds2 = self.connect()
        ds2.expect("WELCOME")
        self.assertTrue(ds2.expect("GAME").startswith("GAME game0001 w DSPlayer"))
        ds2.expect("STATE")
        ds2.expect("READY")

    def test_seek_then_cancel(self):
        ds = self.logged_in()
        ds.send("SEEK 10 0 1")
        self.assertEqual(ds.expect("SEEKING"), "SEEKING Rated 10+0, random opponent")
        wait_for(lambda: self.lichess.seeks)
        ds.send("CANCEL")
        self.assertEqual(ds.expect("IDLE"), "IDLE Cancelled")
        self.assertTrue(self.lichess.seeks[0].closed.is_set())

    def test_seek_turns_into_a_game(self):
        ds = self.logged_in()
        ds.send("SEEK 15 10 0")
        ds.expect("SEEKING")
        wait_for(lambda: self.lichess.seeks)
        self.lichess.seeks[0].end()  # Lichess closes the seek when it's accepted
        self.start_game(ds, "seeked01")
        self.assertIsNone(self.bridge.pending)

    def test_seek_expiring(self):
        ds = self.logged_in()
        ds.send("SEEK 10 0 0")
        ds.expect("SEEKING")
        wait_for(lambda: self.lichess.seeks)
        self.lichess.seeks[0].end()
        self.assertEqual(ds.expect("IDLE"), "IDLE No opponent found")

    def test_blitz_seek_is_refused(self):
        ds = self.logged_in()
        ds.send("SEEK 3 2 0")
        self.assertEqual(ds.expect("ERROR"), "ERROR Time control too fast for the Board API")

    def test_challenge_a_friend_who_declines(self):
        ds = self.logged_in()
        ds.send("CHALLENGE alice 5 3 0")
        ds.expect("SEEKING")
        wait_for(lambda: self.lichess.challenges)
        stream = self.lichess.challenges[0]
        stream.push({"challenge": {"id": "chal0001"}})
        stream.push(None)
        stream.push({"done": "declined"})
        self.assertEqual(ds.expect("IDLE"), "IDLE alice: declined")
        self.assertIn(("challenge", "alice", 300, 3, False), self.lichess.calls)

    def test_play_stockfish(self):
        ds = self.logged_in()
        ds.send("AI 3 5 3 white")
        ds.expect("INFO")
        self.lichess.game("aigame01").push(game_full("aigame01"))
        self.assertTrue(ds.expect("GAME").startswith("GAME aigame01 "))
        self.assertIn(("ai", 3, 300, 3, "white"), self.lichess.calls)

    def test_incoming_challenge(self):
        ds = self.logged_in()
        self.lichess.events.push({"type": "challenge", "challenge": {
            "id": "inc00001", "status": "created", "rated": True, "speed": "rapid",
            "variant": {"key": "standard"}, "color": "white",
            "challenger": {"id": "bob", "name": "Bob", "rating": 1700},
            "destUser": {"id": "dsplayer", "name": "DSPlayer"},
            "timeControl": {"type": "clock", "limit": 600, "increment": 5, "show": "10+5"},
        }})
        self.assertEqual(ds.expect("CHALLENGED"), "CHALLENGED inc00001 Bob 1700 10+5 1 b")
        ds.send("ACCEPT inc00001")
        wait_for(lambda: ("accept_challenge", "inc00001") in self.lichess.calls)

    def test_commands_without_a_game(self):
        ds = self.logged_in()
        ds.send("RESIGN")
        self.assertEqual(ds.expect("ERROR"), "ERROR No game in progress")
        ds.send("FLY")
        self.assertEqual(ds.expect("ERROR"), "ERROR Unknown command FLY")
        ds.send("SEEK ten")
        self.assertEqual(ds.expect("ERROR"), "ERROR Bad SEEK command")


class DS:
    """The DS side of the connection."""

    def __init__(self, sock):
        self.sock = sock
        self.buffer = b""

    def send(self, text):
        self.sock.sendall(text.encode() + b"\n")

    def expect(self, command, timeout=5):
        """Returns the next line starting with command, skipping others."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            while b"\n" in self.buffer:
                raw, self.buffer = self.buffer.split(b"\n", 1)
                text = raw.decode()
                if text.split()[0] == command:
                    return text
            data = self.sock.recv(4096)
            if not data:
                raise AssertionError(f"connection closed while waiting for {command}")
            self.buffer += data
        raise AssertionError(f"timed out waiting for {command}")

    def close(self):
        self.sock.close()


def wait_for(condition, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if condition():
            return
        time.sleep(0.02)
    raise AssertionError("condition not met")


if __name__ == "__main__":
    unittest.main()
