"""Tracks one Lichess game and describes it in DS protocol messages."""

import chess

from .protocol import line

# Number of moves (plies) sent to the DS for the move list.
MOVE_HISTORY = 20


def _player(data):
    """(name, rating text, lowercase id) for a player of a gameFull event."""
    data = data or {}
    if data.get("aiLevel"):
        return f"Stockfish{data['aiLevel']}", "AI", ""
    name = data.get("name") or data.get("id") or "Anonymous"
    rating = data.get("rating")
    rating = "?" if rating is None else f"{rating}{'?' if data.get('provisional') else ''}"
    return name, rating, (data.get("id") or "").lower()


def time_control(full):
    clock = full.get("clock")
    if clock:
        minutes = clock.get("initial", 0) / 60000
        increment = clock.get("increment", 0) // 1000
        return f"{minutes:g}+{increment}"
    if full.get("daysPerTurn"):
        return f"{full['daysPerTurn']}d/move"
    return "unlimited"


def result_text(status, winner):
    """Short (<= 31 characters) description of how a game ended."""
    won = {"white": "White", "black": "Black"}.get(winner)
    lost = {"white": "Black", "black": "White"}.get(winner)
    if status == "mate":
        return f"Checkmate - {won} wins"
    if status == "resign":
        return f"{lost} resigned - {won} wins"
    if status == "outoftime":
        return f"{lost} flagged - {won} wins" if won else "Time out - draw"
    if status == "timeout":
        return f"{lost} left - {won} wins" if won else "Opponent left - draw"
    if status == "stalemate":
        return "Stalemate - draw"
    if status == "draw":
        return "Draw"
    if status == "aborted":
        return "Game aborted"
    if status == "noStart":
        return "Game did not start"
    if won:
        return f"Game over - {won} wins"
    return "Game over"


class GameView:
    """State of a game, built from the Board API game stream."""

    def __init__(self, full, my_id):
        self.id = full["id"]
        self.white_name, self.white_rating, white_id = _player(full.get("white"))
        self.black_name, self.black_rating, black_id = _player(full.get("black"))

        my_id = my_id.lower()
        if white_id == my_id:
            self.my_color = chess.WHITE
        elif black_id == my_id:
            self.my_color = chess.BLACK
        else:
            raise ValueError(f"{my_id} is not playing game {self.id}")

        self.time_control = time_control(full)
        self.has_clock = bool(full.get("clock"))
        self.rated = bool(full.get("rated"))

        fen = full.get("initialFen") or "startpos"
        self.initial_fen = chess.STARTING_FEN if fen == "startpos" else fen
        self.board = chess.Board(self.initial_fen)
        self.moves = []  # UCI strings as sent by Lichess
        self.sans = []   # (move number, color, SAN) for each move

        self.status = "started"
        self.winner = None
        self.wtime = self.btime = None
        self.wdraw = self.bdraw = False
        self.update(full.get("state") or {})

    def update(self, state):
        moves = state.get("moves", "").split()
        if moves[:len(self.moves)] != self.moves:  # takeback: replay from the start
            self.board = chess.Board(self.initial_fen)
            self.moves, self.sans = [], []
        for uci in moves[len(self.moves):]:
            move = self.board.parse_uci(uci)
            self.sans.append((self.board.fullmove_number, self.board.turn, self.board.san(move)))
            self.board.push(move)
            self.moves.append(uci)

        self.status = state.get("status", self.status)
        self.winner = state.get("winner")
        self.wtime = state.get("wtime", self.wtime)
        self.btime = state.get("btime", self.btime)
        self.wdraw = bool(state.get("wdraw"))
        self.bdraw = bool(state.get("bdraw"))

    @property
    def finished(self):
        return self.status not in ("created", "started")

    @property
    def my_turn(self):
        return not self.finished and self.board.turn == self.my_color

    def is_legal(self, uci):
        try:
            return chess.Move.from_uci(uci) in self.board.legal_moves
        except ValueError:
            return False

    def _clock(self, value):
        return -1 if not self.has_clock or value is None else max(0, int(value))

    def header_line(self):
        return line(
            "GAME", self.id, "w" if self.my_color == chess.WHITE else "b",
            self.white_name, self.white_rating, self.black_name, self.black_rating,
            self.time_control, int(self.rated),
        )

    def state_lines(self):
        start = max(0, len(self.sans) - MOVE_HISTORY)
        if start > 0 and self.sans[start][1] == chess.BLACK:
            start -= 1  # start the list on a white move
        recent = self.sans[start:]
        if recent:
            number, color = recent[0][0], recent[0][1]
        else:
            number, color = self.board.fullmove_number, self.board.turn

        draw_offer = "w" if self.wdraw else "b" if self.bdraw else "-"
        lines = [
            line("MOVES", number, "w" if color == chess.WHITE else "b", *(san for _, _, san in recent)),
            line(
                "STATE", self.board.board_fen(), "w" if self.board.turn == chess.WHITE else "b",
                self.moves[-1] if self.moves else "-",
                self._clock(self.wtime), self._clock(self.btime),
                len(self.moves), int(self.board.is_check()), draw_offer,
            ),
            line("LEGAL", *(m.uci() for m in self.board.legal_moves) if self.my_turn else ()),
        ]
        if self.finished:
            winner = {"white": "w", "black": "b"}.get(self.winner, "-")
            lines.append(line("END", self.status, winner, text=result_text(self.status, self.winner)))
        return lines
