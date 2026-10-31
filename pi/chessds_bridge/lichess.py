"""Minimal client for the parts of the Lichess API used by the bridge."""

import json
import logging
import time

import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

from . import __version__

log = logging.getLogger(__name__)

BASE_URL = "https://lichess.org"
TIMEOUT = 15
# Streams send a keep-alive newline every few seconds, so a long silence means
# the connection is dead.
STREAM_TIMEOUT = (10, 90)
# Home routers silently drop idle connections, and reusing one of those fails
# the request. Connections idle for longer than this are not reused.
IDLE_RESET = 30


def _session(token):
    session = requests.Session()
    session.headers.update({
        "Authorization": f"Bearer {token}",
        "User-Agent": f"ChessDS-bridge/{__version__}",
    })
    # Only retry when connecting failed, i.e. Lichess never got the request:
    # retrying a challenge that did arrive would start a second game.
    retry = Retry(total=2, connect=2, read=0, status=0, other=0, backoff_factor=0.5)
    session.mount("https://", HTTPAdapter(max_retries=retry))
    return session


class LichessError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


class Stream:
    """An open ndjson response. Iterating yields one dict per line, or None for
    keep-alive (empty) lines. Can be closed from another thread."""

    def __init__(self, response):
        self.response = response

    def __iter__(self):
        for raw in self.response.iter_lines():
            if not raw:
                yield None
                continue
            try:
                yield json.loads(raw)
            except ValueError:
                log.warning("Ignoring bad JSON from Lichess: %r", raw[:200])

    def close(self):
        self.response.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc_info):
        self.close()


def _bool(value):
    return "true" if value else "false"


def _error_message(response):
    if response.status_code == 429:
        return "Lichess rate limit reached, wait a minute"
    try:
        error = response.json().get("error")
    except (ValueError, AttributeError):
        error = None
    if isinstance(error, dict):
        error = "; ".join(
            f"{key}: {', '.join(map(str, value)) if isinstance(value, list) else value}"
            for key, value in error.items()
        )
    return str(error or f"Lichess HTTP {response.status_code}")


class Lichess:
    def __init__(self, token, base_url=BASE_URL):
        self.base_url = base_url.rstrip("/")
        # Streams get their own connections so the ones for single requests
        # can be dropped while they're idle.
        self.session = _session(token)
        self.stream_session = _session(token)
        self.last_request = time.monotonic()

    def _request(self, method, path, *, stream=False, timeout=TIMEOUT, **kwargs):
        if stream:
            session = self.stream_session
        else:
            session = self.session
            if time.monotonic() - self.last_request > IDLE_RESET:
                session.close()  # forget idle connections; the session stays usable
            self.last_request = time.monotonic()
        try:
            response = session.request(
                method, self.base_url + path, stream=stream, timeout=timeout, **kwargs
            )
        except requests.RequestException as e:
            log.warning("%s %s failed: %s", method, path, e)
            raise LichessError(0, f"Network error ({e.__class__.__name__})") from e
        if response.status_code >= 400:
            message = _error_message(response)
            response.close()
            raise LichessError(response.status_code, message)
        return response

    def _json(self, method, path, **kwargs):
        response = self._request(method, path, **kwargs)
        try:
            return response.json()
        finally:
            response.close()

    def _post(self, path, **kwargs):
        self._request("POST", path, **kwargs).close()

    def _stream(self, method, path, **kwargs):
        return Stream(self._request(method, path, stream=True, timeout=STREAM_TIMEOUT, **kwargs))

    # Account and streams

    def account(self):
        return self._json("GET", "/api/account")

    def stream_events(self):
        return self._stream("GET", "/api/stream/event")

    def stream_game(self, game_id):
        return self._stream("GET", f"/api/board/game/stream/{game_id}")

    # Starting games

    def seek(self, minutes, increment, rated):
        """Real-time seek. The stream stays open until someone accepts; closing
        it cancels the seek."""
        data = {"time": minutes, "increment": increment, "rated": _bool(rated)}
        return self._stream("POST", "/api/board/seek", data=data)

    def challenge(self, username, limit_seconds, increment, rated):
        """Challenge a player. The stream ends with {"done": "accepted"} (or
        "declined", ...) once they answer."""
        data = {
            "clock.limit": limit_seconds,
            "clock.increment": increment,
            "rated": _bool(rated),
            "keepAliveStream": "true",
        }
        return self._stream("POST", f"/api/challenge/{username}", data=data)

    def challenge_ai(self, level, limit_seconds, increment, color):
        data = {
            "level": level,
            "clock.limit": limit_seconds,
            "clock.increment": increment,
            "color": color,
        }
        return self._json("POST", "/api/challenge/ai", data=data)

    def cancel_challenge(self, challenge_id):
        self._post(f"/api/challenge/{challenge_id}/cancel")

    def accept_challenge(self, challenge_id):
        self._post(f"/api/challenge/{challenge_id}/accept")

    def decline_challenge(self, challenge_id):
        self._post(f"/api/challenge/{challenge_id}/decline", data={"reason": "generic"})

    # Playing

    def move(self, game_id, uci):
        self._post(f"/api/board/game/{game_id}/move/{uci}")

    def resign(self, game_id):
        self._post(f"/api/board/game/{game_id}/resign")

    def abort(self, game_id):
        self._post(f"/api/board/game/{game_id}/abort")

    def draw(self, game_id, accept):
        self._post(f"/api/board/game/{game_id}/draw/{'yes' if accept else 'no'}")

    def claim_victory(self, game_id):
        self._post(f"/api/board/game/{game_id}/claim-victory")
