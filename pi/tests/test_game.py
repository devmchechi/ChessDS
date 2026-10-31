import unittest

from chessds_bridge.game import GameView, result_text, time_control
from chessds_bridge.protocol import ascii_text, line, parse, token


def game_full(moves="", status="started", white_id="me", black_id="opp", **extra):
    full = {
        "type": "gameFull",
        "id": "abcd1234",
        "rated": False,
        "clock": {"initial": 600000, "increment": 5000},
        "white": {"id": white_id, "name": white_id.title(), "rating": 1500},
        "black": {"id": black_id, "name": black_id.title(), "rating": 1600, "provisional": True},
        "initialFen": "startpos",
        "state": {"type": "gameState", "moves": moves, "wtime": 600000, "btime": 590000,
                  "status": status},
    }
    full.update(extra)
    return full


def by_command(lines):
    return {text.split()[0]: text for text in lines}


class ProtocolTest(unittest.TestCase):
    def test_tokens_have_no_spaces(self):
        self.assertEqual(token("Stockfish level 3"), "Stockfish_level_3")
        self.assertEqual(token(""), "-")

    def test_text_is_ascii_on_one_line(self):
        self.assertEqual(ascii_text("Gut gemacht! ♞\nNächstes Mal"), "Gut gemacht! Nachstes Mal")

    def test_line_and_parse(self):
        text = line("CHAT", "alice", text="good game")
        self.assertEqual(text, "CHAT alice good game")
        self.assertEqual(parse("move e2e4\r"), ("MOVE", ["e2e4"]))
        self.assertEqual(parse("   "), (None, []))


class GameViewTest(unittest.TestCase):
    def test_start_position_as_white(self):
        game = GameView(game_full(), "me")
        lines = by_command([game.header_line(), *game.state_lines()])
        self.assertEqual(lines["GAME"], "GAME abcd1234 w Me 1500 Opp 1600? 10+5 0")
        self.assertEqual(
            lines["STATE"],
            "STATE rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - 600000 590000 0 0 -",
        )
        self.assertEqual(len(lines["LEGAL"].split()) - 1, 20)
        self.assertEqual(lines["MOVES"], "MOVES 1 w")
        self.assertNotIn("END", lines)

    def test_no_legal_moves_on_opponent_turn(self):
        game = GameView(game_full("e2e4"), "me")
        lines = by_command(game.state_lines())
        self.assertEqual(lines["LEGAL"], "LEGAL")
        self.assertIn(" b e2e4 ", lines["STATE"])
        self.assertEqual(lines["MOVES"], "MOVES 1 w e4")

    def test_playing_black(self):
        game = GameView(game_full("e2e4", white_id="opp", black_id="me"), "ME")
        self.assertTrue(game.my_turn)
        self.assertTrue(game.header_line().startswith("GAME abcd1234 b "))

    def test_castling_promotion_and_check(self):
        fen = "4k3/1P6/8/8/8/8/8/R3K2R w KQ - 0 40"
        game = GameView(game_full(initialFen=fen), "me")
        legal = by_command(game.state_lines())["LEGAL"].split()[1:]
        self.assertIn("e1g1", legal)
        self.assertIn("e1c1", legal)
        self.assertEqual({m for m in legal if m.startswith("b7b8")},
                         {"b7b8q", "b7b8r", "b7b8b", "b7b8n"})

        game.update({"moves": "b7b8q", "status": "started"})
        lines = by_command(game.state_lines())
        self.assertEqual(lines["MOVES"], "MOVES 40 w b8=Q+")
        self.assertTrue(lines["STATE"].endswith(" 1 1 -"))  # ply 1, check

    def test_move_list_starts_on_white_move(self):
        moves = "e2e4 e7e5 g1f3 b8c6 f1c4 g8f6 d2d3 f8c5 c2c3 d7d6 " \
                "e1g1 e8g8 b1d2 a7a6 c4b3 c5a7 h2h3 h7h6 f1e1 f8e8 d2f1"
        game = GameView(game_full(moves), "me")
        lines = by_command(game.state_lines())
        fields = lines["MOVES"].split()
        self.assertEqual(fields[1:3], ["1", "w"])  # 21 plies, so all of them fit
        game.update({"moves": moves + " c8e6", "status": "started"})
        fields = by_command(game.state_lines())["MOVES"].split()
        self.assertEqual(fields[1:3], ["2", "w"])
        self.assertEqual(fields[3], "Nf3")
        self.assertEqual(fields[-1], "Be6")

    def test_takeback_replays(self):
        game = GameView(game_full("e2e4 e7e5"), "me")
        game.update({"moves": "e2e4", "status": "started"})
        self.assertEqual(game.moves, ["e2e4"])
        self.assertEqual(len(game.sans), 1)

    def test_finished_game(self):
        game = GameView(game_full("f2f3 e7e5 g2g4 d8h4", status="mate", winner="black"), "me")
        game.update({"moves": "f2f3 e7e5 g2g4 d8h4", "status": "mate", "winner": "black"})
        lines = by_command(game.state_lines())
        self.assertEqual(lines["END"], "END mate b Checkmate - Black wins")
        self.assertEqual(lines["LEGAL"], "LEGAL")
        self.assertIn(" 600000 590000 ", lines["STATE"])  # clocks kept when omitted

    def test_draw_offer_and_no_clock(self):
        full = game_full("e2e4", white_id="opp", black_id="me")
        del full["clock"]
        full["state"]["wdraw"] = True
        game = GameView(full, "me")
        state = by_command(game.state_lines())["STATE"]
        self.assertTrue(state.endswith(" -1 -1 1 0 w"))
        self.assertEqual(time_control(full), "unlimited")

    def test_ai_opponent_and_wrong_player(self):
        full = game_full(black_id="x")
        full["black"] = {"aiLevel": 3}
        game = GameView(full, "me")
        self.assertIn(" Stockfish3 AI ", game.header_line())
        with self.assertRaises(ValueError):
            GameView(game_full(), "someone_else")

    def test_result_texts_fit_on_the_ds(self):
        for status in ("mate", "resign", "outoftime", "timeout", "stalemate", "draw",
                       "aborted", "noStart", "cheat"):
            for winner in ("white", "black", None):
                self.assertLessEqual(len(result_text(status, winner)), 31, (status, winner))


if __name__ == "__main__":
    unittest.main()
