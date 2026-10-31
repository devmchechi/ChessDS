"""Connects the DS (one TCP client at a time) with the Lichess Board API."""

import hmac
import logging
import re
import threading
import time

from . import protocol
from .game import GameView
from .lichess import LichessError
from .protocol import line

log = logging.getLogger(__name__)

USERNAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{1,29}$")
ID_RE = re.compile(r"^[A-Za-z0-9]{4,16}$")
UCI_RE = re.compile(r"^[a-h][1-8][a-h][1-8][qrbn]?$")
COLORS = ("random", "white", "black")
# After a seek or challenge stream closes, how long to wait for the game to start.
GAME_START_WAIT = 10


class UserError(Exception):
    """A problem with a DS request; reported to the DS as ERROR."""


def estimated_seconds(minutes, increment):
    """Lichess' estimated game duration, which decides bullet/blitz/rapid."""
    return minutes * 60 + 40 * increment


class Pending:
    """A seek or outgoing challenge waiting for an opponent."""

    def __init__(self, kind, description):
        self.kind = kind
        self.description = description
        self.cancelled = threading.Event()
        self.stream = None
        self.challenge_id = None

    def cancel(self):
        self.cancelled.set()
        if self.stream is not None:
            self.stream.close()


class Bridge:
    def __init__(self, lichess, secret, friends=()):
        self.li = lichess
        self.secret = secret
        self.friends = [name for name in friends if USERNAME_RE.match(name)]
        self.account = None
        self.stopping = threading.Event()

        # Everything below is protected by self.lock. Lines are sent to the DS
        # while holding it so they arrive in a consistent order.
        self.lock = threading.RLock()
        self.client = None
        self.game_id = None        # game shown on the DS (set before gameFull arrives)
        self.game = None           # GameView of that game
        self.gone_deadline = None  # monotonic time when a win can be claimed
        self.pending = None        # Pending seek or challenge
        self.incoming = {}         # challenge id -> CHALLENGED line
        self.accepted = set()      # challenge ids the DS accepted
        self.game_threads = {}

    # Lifecycle

    def start(self):
        self.account = self.li.account()
        log.info("Logged in to Lichess as %s", self.account["username"])
        self._spawn(self._event_loop, "lichess-events")

    def stop(self):
        self.stopping.set()
        with self.lock:
            pending = self.pending
        if pending is not None:
            pending.cancel()

    @property
    def my_id(self):
        return self.account["id"].lower()

    def _spawn(self, target, name, *args):
        thread = threading.Thread(target=target, args=args, name=name, daemon=True)
        thread.start()
        return thread

    # The DS connection

    def check_hello(self, args):
        """HELLO <protocol version> <secret>"""
        if len(args) < 2:
            return False
        if args[0] != str(protocol.PROTOCOL_VERSION):
            log.warning("DS speaks protocol %s, bridge speaks %s", args[0], protocol.PROTOCOL_VERSION)
        return hmac.compare_digest(args[1].encode(), self.secret.encode())

    def attach(self, client):
        """Makes client the DS we talk to and sends it the current state."""
        with self.lock:
            old, self.client = self.client, client
            if old is not None and old is not client:
                old.close()
            self._send(*self._sync_lines())

    def detach(self, client):
        with self.lock:
            if self.client is client:
                self.client = None

    def _send(self, *lines):
        # Caller holds self.lock.
        if self.client is not None and lines:
            self.client.send_lines(lines)

    def _sync_lines(self):
        lines = [
            line("WELCOME", self.account["username"], protocol.PROTOCOL_VERSION),
            line("FRIENDS", *self.friends),
            *self.incoming.values(),
        ]
        if self.pending is not None:
            lines.append(line("SEEKING", text=self.pending.description))
        if self.game is not None:
            lines.append(self.game.header_line())
            lines.extend(self.game.state_lines())
            if self.gone_deadline is not None and not self.game.finished:
                lines.append(self._gone_line())
        lines.append(line("READY"))
        return lines

    def _gone_line(self):
        return line("GONE", max(0, round(self.gone_deadline - time.monotonic())))

    def _info(self, text):
        with self.lock:
            self._send(line("INFO", text=text))

    def _error(self, text):
        log.info("Error for the DS: %s", text)
        with self.lock:
            self._send(line("ERROR", text=text))

    # Commands from the DS

    def handle(self, command, args):
        handler = self.COMMANDS.get(command)
        if handler is None:
            self._error(f"Unknown command {command}")
            return
        try:
            handler(self, args)
        except UserError as e:
            self._error(str(e))
        except LichessError as e:
            self._error(str(e))
        except (ValueError, IndexError):
            self._error(f"Bad {command} command")

    def cmd_ping(self, args):
        with self.lock:
            self._send("PONG")

    def _check_clock(self, minutes, increment, minimum):
        if not (0 < minutes <= 180 and 0 <= increment <= 60):
            raise UserError("Invalid time control")
        if estimated_seconds(minutes, increment) < minimum:
            raise UserError("Time control too fast for the Board API")

    def _new_pending(self, kind, description):
        with self.lock:
            if self.pending is not None:
                raise UserError("Already waiting for an opponent")
            if self.game is not None and not self.game.finished:
                raise UserError("Finish your current game first")
            pending = Pending(kind, description)
            self.pending = pending
            self._send(line("SEEKING", text=description))
            return pending

    def _finish_pending(self, pending, message):
        with self.lock:
            if self.pending is not pending:
                return  # a game started, or it was already finished
            self.pending = None
            self._send(line("IDLE", text=message))

    def _wait_for_game(self, pending, message):
        """The seek/challenge stream closed; give the game a moment to start."""
        deadline = time.monotonic() + GAME_START_WAIT
        while time.monotonic() < deadline and not self.stopping.is_set():
            with self.lock:
                if self.pending is not pending:
                    return
            time.sleep(0.2)
        self._finish_pending(pending, message)

    def cmd_seek(self, args):
        """SEEK <minutes> <increment> <rated 0|1>: play a random opponent."""
        minutes, increment, rated = int(args[0]), int(args[1]), args[2] == "1"
        # Seeks from the Board API must be rapid or slower.
        self._check_clock(minutes, increment, minimum=480)
        kind = "Rated" if rated else "Casual"
        # The description is shown on the DS, which has room for 30 characters.
        pending = self._new_pending("seek", f"{kind} {minutes}+{increment}, random opponent")
        self._spawn(self._seek_loop, "seek", pending, minutes, increment, rated)

    def _seek_loop(self, pending, minutes, increment, rated):
        try:
            with self.li.seek(minutes, increment, rated) as stream:
                pending.stream = stream
                if pending.cancelled.is_set():
                    return
                for _ in stream:
                    if pending.cancelled.is_set():
                        return
        except LichessError as e:
            self._finish_pending(pending, f"Seek failed: {e}")
            return
        except Exception as e:  # closing the stream from cancel() lands here too
            if not pending.cancelled.is_set():
                log.warning("Seek stream failed: %s", e)
                self._finish_pending(pending, "Seek failed (network error)")
            return
        if not pending.cancelled.is_set():
            # Lichess closes the stream when someone accepts (or the seek expires).
            self._wait_for_game(pending, "No opponent found")

    def cmd_challenge(self, args):
        """CHALLENGE <username> <minutes> <increment> <rated 0|1>"""
        username, minutes, increment, rated = args[0], int(args[1]), int(args[2]), args[3] == "1"
        if not USERNAME_RE.match(username):
            raise UserError("Invalid username")
        self._check_clock(minutes, increment, minimum=180)
        pending = self._new_pending("challenge", f"Challenged {username} ({minutes}+{increment})")
        self._spawn(self._challenge_loop, "challenge", pending, username, minutes, increment, rated)

    def _challenge_loop(self, pending, username, minutes, increment, rated):
        answer = None
        try:
            with self.li.challenge(username, minutes * 60, increment, rated) as stream:
                pending.stream = stream
                for message in stream:
                    if pending.cancelled.is_set():
                        return
                    if not message:
                        continue
                    challenge = message.get("challenge") or message
                    if challenge.get("id"):
                        pending.challenge_id = challenge["id"]
                    if "done" in message:
                        answer = message["done"]
                        break
        except LichessError as e:
            self._finish_pending(pending, f"Challenge failed: {e}")
            return
        except Exception as e:
            if not pending.cancelled.is_set():
                log.warning("Challenge stream failed: %s", e)
                self._finish_pending(pending, "Challenge failed (network error)")
            return
        if pending.cancelled.is_set():
            return
        if answer == "accepted":
            self._wait_for_game(pending, "Challenge accepted, but no game started")
        else:
            self._finish_pending(pending, f"{username}: {answer or 'no answer'}")

    def cmd_cancel(self, args):
        with self.lock:
            pending = self.pending
            if pending is None:
                self._send(line("IDLE"))
                return
        pending.cancel()
        if pending.kind == "challenge" and pending.challenge_id:
            try:
                self.li.cancel_challenge(pending.challenge_id)
            except LichessError as e:
                log.info("Could not cancel challenge: %s", e)
        self._finish_pending(pending, "Cancelled")

    def cmd_ai(self, args):
        """AI <level 1-8> <minutes> <increment> [random|white|black]"""
        level, minutes, increment = int(args[0]), int(args[1]), int(args[2])
        color = args[3] if len(args) > 3 else "random"
        if not 1 <= level <= 8:
            raise UserError("Stockfish level must be 1 to 8")
        if color not in COLORS:
            raise UserError("Color must be random, white or black")
        self._check_clock(minutes, increment, minimum=180)
        with self.lock:
            if self.pending is not None:
                raise UserError("Cancel your seek first")
            if self.game is not None and not self.game.finished:
                raise UserError("Finish your current game first")
            self._send(line("INFO", text=f"Starting a game vs Stockfish level {level}"))
        game = self.li.challenge_ai(level, minutes * 60, increment, color)
        self._activate_game(game["id"], force=True)

    def _challenge_id(self, args):
        challenge_id = args[0]
        if not ID_RE.match(challenge_id):
            raise UserError("Invalid challenge id")
        with self.lock:
            self.incoming.pop(challenge_id, None)
        return challenge_id

    def cmd_accept(self, args):
        challenge_id = self._challenge_id(args)
        with self.lock:
            if self.game is not None and not self.game.finished:
                raise UserError("Finish your current game first")
            self.accepted.add(challenge_id)
        self.li.accept_challenge(challenge_id)

    def cmd_decline(self, args):
        self.li.decline_challenge(self._challenge_id(args))

    def _current_game(self):
        with self.lock:
            game = self.game
        if game is None or game.finished:
            raise UserError("No game in progress")
        return game

    def cmd_move(self, args):
        uci = args[0]
        with self.lock:
            game = self.game
            if game is None or game.finished:
                raise UserError("No game in progress")
            if not game.my_turn or not UCI_RE.match(uci) or not game.is_legal(uci):
                # Undo the move the DS shows optimistically.
                self._send(*game.state_lines())
                raise UserError("Not your turn" if not game.my_turn else f"Illegal move {uci}")
        try:
            self.li.move(game.id, uci)
        except LichessError:
            with self.lock:
                if self.game is game:
                    self._send(*game.state_lines())
            raise

    def cmd_resign(self, args):
        self.li.resign(self._current_game().id)

    def cmd_abort(self, args):
        self.li.abort(self._current_game().id)

    def cmd_draw(self, args):
        """DRAW yes (offer or accept) / DRAW no (decline)"""
        self.li.draw(self._current_game().id, args[0] == "yes")

    def cmd_claim(self, args):
        self.li.claim_victory(self._current_game().id)

    def cmd_leave(self, args):
        """The DS left the result screen of a finished game."""
        with self.lock:
            if self.game is not None and self.game.finished:
                self.game_id = self.game = self.gone_deadline = None

    COMMANDS = {
        "PING": cmd_ping,
        "SEEK": cmd_seek,
        "CHALLENGE": cmd_challenge,
        "CANCEL": cmd_cancel,
        "AI": cmd_ai,
        "ACCEPT": cmd_accept,
        "DECLINE": cmd_decline,
        "MOVE": cmd_move,
        "RESIGN": cmd_resign,
        "ABORT": cmd_abort,
        "DRAW": cmd_draw,
        "CLAIM": cmd_claim,
        "LEAVE": cmd_leave,
    }

    # Lichess event stream

    def _event_loop(self):
        delay = 1
        while not self.stopping.is_set():
            try:
                with self.li.stream_events() as stream:
                    log.info("Listening for Lichess events")
                    delay = 1
                    for event in stream:
                        if self.stopping.is_set():
                            return
                        if event:
                            self.on_event(event)
            except LichessError as e:
                log.warning("Event stream error: %s", e)
                if e.status == 429:
                    delay = 60
            except Exception as e:
                log.warning("Event stream error: %s", e)
            if self.stopping.wait(delay):
                return
            delay = min(delay * 2, 60)

    def on_event(self, event):
        kind = event.get("type")
        if kind == "gameStart":
            self._on_game_start(event.get("game") or {})
        elif kind == "challenge":
            self._on_challenge(event.get("challenge") or {})
        elif kind in ("challengeCanceled", "challengeDeclined"):
            challenge_id = (event.get("challenge") or {}).get("id")
            with self.lock:
                if self.incoming.pop(challenge_id, None) is not None:
                    self._send(line("CHALLENGE_GONE", challenge_id))
        # gameFinish needs nothing: the game stream reports the result.

    def _on_game_start(self, game):
        game_id = game.get("gameId") or game.get("id")
        if not game_id:
            return
        if (game.get("compat") or {}).get("board") is False:
            self._info(f"Game {game_id} can't be played on the DS")
            return
        with self.lock:
            # Our own seek/challenge, or a challenge the DS accepted.
            expected = self.pending is not None or game_id in self.accepted
            self.accepted.discard(game_id)
        if game.get("speed") == "correspondence" and not expected:
            log.info("Ignoring correspondence game %s", game_id)
            return
        self._activate_game(game_id, force=expected)

    def _activate_game(self, game_id, force=False):
        with self.lock:
            if self.game_id == game_id:
                return
            busy = self.game_id is not None and (self.game is None or not self.game.finished)
            if busy and not force:
                log.info("Game %s started while %s is in progress; ignoring it", game_id, self.game_id)
                return
            log.info("Showing game %s on the DS", game_id)
            self.game_id = game_id
            self.game = None
            self.gone_deadline = None
            self.pending = None  # the seek or challenge became this game
            thread = self.game_threads.get(game_id)
            if thread is None or not thread.is_alive():
                self.game_threads[game_id] = self._spawn(self._game_loop, f"game-{game_id}", game_id)

    def _on_challenge(self, challenge):
        challenge_id = challenge.get("id")
        challenger = challenge.get("challenger") or {}
        if not challenge_id or (challenge.get("destUser") or {}).get("id", "").lower() != self.my_id:
            return  # a challenge we sent
        name = challenger.get("name") or challenger.get("id") or "Someone"

        variant = (challenge.get("variant") or {}).get("key", "standard")
        if variant not in ("standard", "fromPosition"):
            self._info(f"{name} challenged you to {variant}, which the DS can't play")
            return
        if challenge.get("speed") in ("bullet", "ultraBullet"):
            self._info(f"{name} challenged you to bullet, which the Board API can't play")
            return

        clock = challenge.get("timeControl") or {}
        if clock.get("type") == "clock":
            time_text = clock.get("show") or f"{clock.get('limit', 0) // 60}+{clock.get('increment', 0)}"
        elif clock.get("type") == "correspondence":
            time_text = f"{clock.get('daysPerTurn', '?')}d/move"
        else:
            time_text = "unlimited"

        rating = challenger.get("rating")
        rating = "?" if rating is None else f"{rating}{'?' if challenger.get('provisional') else ''}"
        # "color" is the challenger's choice; tell the DS what it will play.
        my_color = {"white": "b", "black": "w"}.get(challenge.get("color"), "r")
        message = line("CHALLENGED", challenge_id, name, rating, time_text,
                       int(bool(challenge.get("rated"))), my_color)
        with self.lock:
            self.incoming[challenge_id] = message
            self._send(message)

    # Game streams

    def _is_current(self, game_id):
        with self.lock:
            return self.game_id == game_id

    def _game_loop(self, game_id):
        delay = 1
        while not self.stopping.is_set():
            try:
                with self.li.stream_game(game_id) as stream:
                    for message in stream:
                        if message:
                            self._on_game_message(game_id, message)
                        if not self._is_current(game_id):
                            return
            except LichessError as e:
                log.warning("Game %s stream error: %s", game_id, e)
                if e.status in (400, 404):
                    with self.lock:
                        if self.game_id == game_id and self.game is None:
                            self.game_id = None
                            self._send(line("ERROR", text=f"Can't open game {game_id}"))
                    return
            except Exception as e:
                log.warning("Game %s stream error: %s", game_id, e)

            with self.lock:
                if self.game_id != game_id or (self.game is not None and self.game.finished):
                    return
            # The stream dropped during the game: reconnect.
            if self.stopping.wait(delay):
                return
            delay = min(delay * 2, 30)

    def _on_game_message(self, game_id, message):
        kind = message.get("type")
        with self.lock:
            if self.game_id != game_id:
                return

            if kind == "gameFull":
                try:
                    self.game = GameView(message, self.my_id)
                except ValueError as e:
                    log.warning("Can't show game %s: %s", game_id, e)
                    self.game_id = None
                    return
                self._send(self.game.header_line(), *self.game.state_lines())

            elif kind == "gameState" and self.game is not None:
                was_finished = self.game.finished
                self.game.update(message)
                if not was_finished:
                    if self.game.finished:
                        self.gone_deadline = None
                    self._send(*self.game.state_lines())

            elif kind == "chatLine" and message.get("room") == "player":
                user = message.get("username") or "?"
                if user.lower() != self.my_id:
                    self._send(line("CHAT", user, text=message.get("text", "")))

            elif kind == "opponentGone":
                if message.get("gone"):
                    seconds = int(message.get("claimWinInSeconds") or 0)
                    self.gone_deadline = time.monotonic() + seconds
                    self._send(self._gone_line())
                elif self.gone_deadline is not None:
                    self.gone_deadline = None
                    self._send(line("GONE", "back"))
