"""Render ChessDS screens on a PC, without a DS.

    python ds/tools/preview/screens.py

Compiles the real DS code (ds/source) for the PC together with sim.c, which
fakes WiFi, the Pi and the buttons, and plays a scripted session. The Pi's
messages come from the real bridge code (pi/chessds_bridge). Each step is saved
as a PNG of both screens in docs/screenshots/.

Needs Pillow and python-chess (pip install pillow chess), and either gcc or
Docker (the code is then compiled in an Alpine container).
"""

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
OUT_DIR = REPO / "docs" / "screenshots"
sys.path.insert(0, str(REPO / "pi"))

try:
    from chessds_bridge.game import GameView
    from chessds_bridge.protocol import line
except ImportError as e:
    sys.exit(f"{e}: install python-chess (pip install chess)")

W, H = 256, 192
SCALE = 2
GAP = 24     # hinge between the screens
MARGIN = 12

SOURCES = ["game.c", "lobby.c", "gfx.c", "font.c", "font_data.c", "ui.c", "pieces.c", "board.c"]


# The scripted session


def square(name):
    """Center of a square on the bottom screen, white at the bottom."""
    file, rank = ord(name[0]) - ord("a"), int(name[1]) - 1
    return f"touch {file * 24 + 12} {(7 - rank) * 24 + 12}"


def menu_item(index, count):
    """Tap entry `index` of a menu with `count` entries (layout of lobby.c)."""
    pitch = min(28, (192 - 4 - 42) // count)
    return f"touch 120 {42 + index * pitch + pitch // 2}"


def panel_button(slot):
    return f"touch 224 {slot * 48 + 24}"


def game_full(moves, fen="startpos", black=("bob", "Bob", 1704), white=("dsplayer", "DSPlayer", 1532),
              **state):
    def player(p):
        return p if isinstance(p, dict) else {"id": p[0], "name": p[1], "rating": p[2]}
    st = {"type": "gameState", "moves": moves, "wtime": 600000, "btime": 600000, "status": "started"}
    st.update(state)
    return {"type": "gameFull", "id": "q7ZvsdUF", "rated": True,
            "clock": {"initial": 600000, "increment": 5000},
            "white": player(white), "black": player(black), "initialFen": fen, "state": st}


def script():
    s = []

    def pi(*lines):
        s.extend(f"pi {text}" for text in lines)

    s += ["wait 20", "dump 01_connecting", "wait 110", "expect HELLO 1 changeme"]
    pi("WELCOME DSPlayer 1", "FRIENDS alice bob", "READY")
    s += ["wait 2", "dump 02_main_menu"]

    s += [menu_item(0, 4), "wait 1", "dump 03_time_control", menu_item(1, 6), "expect SEEK 10 5 0"]
    pi(line("SEEKING", text="Casual 10+5, random opponent"))
    s += ["wait 2", "dump 04_waiting_for_opponent", "key B", "expect CANCEL"]
    pi(line("IDLE", text="Cancelled"), line("CHALLENGED", "chal0001", "Bob", "1704", "10+5", 1, "w"))
    s += ["wait 2", "dump 05_incoming_challenge", menu_item(0, 2), "expect ACCEPT chal0001"]

    # A game against Bob, playing white.
    giuoco = ("e2e4 e7e5 g1f3 b8c6 f1c4 f8c5 c2c3 g8f6 d2d4 e5d4 c3d4 c5b4 "
              "c1d2 b4d2 b1d2 d7d5 e4d5 f6d5 d1b3 c6e7 e1g1 e8g8 f1e1 c7c6").split()
    game = GameView(game_full(""), "dsplayer")
    pi(game.header_line(), *game.state_lines())
    s += ["wait 2", square("e2"), "wait 1", "dump 06_your_move"]

    game.update({"moves": " ".join(giuoco[:12]), "wtime": 541000, "btime": 566000, "status": "started"})
    pi(*game.state_lines())
    s += ["wait 2", square("c1"), "wait 1", "dump 07_check"]
    s += [square("d2"), "expect MOVE c1d2", "wait 1", "dump 08_move_sent"]

    game.update({"moves": " ".join(giuoco), "wtime": 402000, "btime": 455000, "status": "started",
                 "bdraw": True})
    pi(*game.state_lines(), line("CHAT", "Bob", text="draw?"))
    s += ["wait 2", square("b3"), "wait 1", "dump 09_draw_offer"]
    s += [panel_button(3), "expect DRAW no", panel_button(0), "wait 1", "dump 10_confirm_resign"]

    game.update({"moves": " ".join(giuoco), "wtime": 398000, "btime": 455000, "status": "resign",
                 "winner": "white"})
    pi(*game.state_lines())
    s += ["wait 2", "dump 11_game_over", panel_button(0), "expect LEAVE", "wait 2"]

    # Promotion in a game against Stockfish.
    s += [menu_item(1, 4), "wait 1", "dump 12_stockfish_level", menu_item(2, 9), "wait 1",
          menu_item(3, 8), "expect AI 3 10 0 random"]
    ai = GameView(game_full("", fen="6k1/1P3ppp/8/8/8/8/5PPP/6K1 w - - 0 35",
                            black={"aiLevel": 3}), "dsplayer")
    pi(line("INFO", text="Starting a game vs Stockfish level 3"), ai.header_line(), *ai.state_lines())
    s += ["wait 2", square("b7"), square("b8"), "wait 1", "dump 13_promotion"]
    s += ["key RIGHT", "key RIGHT", "key RIGHT", "key A", "expect MOVE b7b8n", "wait 1"]
    return "\n".join(s) + "\n"


# Build and run


def run_simulation(work):
    (work / "script.txt").write_text(script())
    flags = "-std=gnu11 -O1 -Wall -Wextra -Wno-unused-parameter"
    if shutil.which("gcc"):
        src, tools = REPO / "ds" / "source", HERE
        commands = [
            f"gcc {flags} -I{tools}/stubs -I{src} -Dmain=ds_main -c {src}/main.c -o {work}/main.o",
            f"gcc {flags} -I{tools}/stubs -I{src} -o {work}/sim {tools}/sim.c "
            + " ".join(f"{src}/{name}" for name in SOURCES) + f" {work}/main.o",
        ]
        for command in commands:
            subprocess.run(command, shell=True, check=True)
        subprocess.run([str(work / "sim")], stdin=open(work / "script.txt"), cwd=work, check=True)
    else:
        build = (
            "apk add --no-cache gcc musl-dev >/dev/null && "
            f"gcc {flags} -Itools/stubs -Isrc -Dmain=ds_main -c src/main.c -o /tmp/main.o && "
            f"gcc {flags} -Itools/stubs -Isrc -o /tmp/sim tools/sim.c "
            + " ".join(f"src/{name}" for name in SOURCES) + " /tmp/main.o && "
            "cd /out && /tmp/sim < script.txt"
        )
        subprocess.run([
            "docker", "run", "--rm",
            "-v", f"{REPO / 'ds' / 'source'}:/w/src:ro",
            "-v", f"{HERE}:/w/tools:ro",
            "-v", f"{work}:/out",
            "-w", "/w", "alpine:3.20", "sh", "-c", build,
        ], check=True)


# Rendering


def bitmap(data):
    """A 256x192 image from 15-bit DS pixels."""
    rgb = bytearray()
    for i in range(0, len(data), 2):
        c = data[i] | data[i + 1] << 8
        rgb += bytes(((c & 31) * 255 // 31, (c >> 5 & 31) * 255 // 31, (c >> 10 & 31) * 255 // 31))
    return Image.frombytes("RGB", (W, H), bytes(rgb))


def render(screen_file):
    data = screen_file.read_bytes()
    size = W * H * 2
    top, bottom = bitmap(data[:size]), bitmap(data[size:2 * size])

    ds = Image.new("RGB", (W + 2 * MARGIN, 2 * H + GAP + 2 * MARGIN), (22, 22, 24))
    ds.paste(top, (MARGIN, MARGIN))
    ds.paste(bottom, (MARGIN, MARGIN + H + GAP))
    return ds.resize((ds.width * SCALE, ds.height * SCALE), Image.NEAREST)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        run_simulation(work)
        shots = sorted(work.glob("*.screen"))
        images = []
        for shot in shots:
            image = render(shot)
            image.save(OUT_DIR / f"{shot.stem}.png")
            images.append(image)

    # Contact sheet with every screen at half size.
    thumb_w, thumb_h = images[0].width // 2, images[0].height // 2
    per_row = 5
    rows = (len(images) + per_row - 1) // per_row
    sheet = Image.new("RGB", (per_row * thumb_w, rows * thumb_h), (20, 20, 24))
    for i, image in enumerate(images):
        sheet.paste(image.resize((thumb_w, thumb_h), Image.LANCZOS),
                    ((i % per_row) * thumb_w, (i // per_row) * thumb_h))
    sheet.save(OUT_DIR / "overview.png")
    print(f"Wrote {len(images)} screens and overview.png to {OUT_DIR}")


if __name__ == "__main__":
    main()
