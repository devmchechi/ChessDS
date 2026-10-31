# DS/bridge protocol (version 1)

The DS connects to the bridge over TCP (port 5555 by default). Both sides send
lines of printable ASCII ending in `\n`. Fields are separated by spaces. Only
the last field, written `<text...>` below, may contain spaces. Names never
contain spaces. A field that would be empty is sent as `-`.

The bridge serves one DS at a time. A new DS that logs in replaces the old one.

## From the DS

| Message | Meaning |
| --- | --- |
| `HELLO <version> <secret>` | First line. The bridge answers `WELCOME ...` or `DENIED <text...>` and disconnects. |
| `PING` | Keep-alive, sent every 5 s. Answered with `PONG`. The bridge drops a DS that is silent for 60 s. |
| `SEEK <minutes> <increment> <rated 0/1>` | Play a random opponent. Must be rapid or slower: `minutes*60 + 40*increment >= 480`. |
| `AI <level 1-8> <minutes> <increment> [random/white/black]` | Play Stockfish. |
| `CHALLENGE <username> <minutes> <increment> <rated 0/1>` | Challenge a player. |
| `CANCEL` | Cancel the pending seek or challenge. Answered with `IDLE`. |
| `ACCEPT <challenge id>` / `DECLINE <challenge id>` | Answer an incoming challenge. |
| `MOVE <uci>` | Play a move, e.g. `e2e4`, `e7e8q`, castling `e1g1`. |
| `RESIGN`, `ABORT`, `CLAIM` | Resign, abort (before both sides moved), claim the win after the opponent left. |
| `DRAW yes` / `DRAW no` | Offer or accept a draw / decline the opponent's offer. |
| `LEAVE` | The DS left the result screen. The bridge forgets the finished game. |

## From the bridge

| Message | Meaning |
| --- | --- |
| `WELCOME <username> <version>` | Logged in. The current state follows, ending with `READY`. |
| `READY` | End of the state sent after `WELCOME`. |
| `DENIED <text...>` | Wrong secret. The connection is closed. |
| `PONG` | Answer to `PING`. |
| `INFO <text...>` / `ERROR <text...>` | A message to show the player. |
| `FRIENDS <name> <name> ...` | Players for the challenge menu (`CHESSDS_FRIENDS`). |
| `SEEKING <text...>` | Waiting for an opponent (seek or outgoing challenge). |
| `IDLE <text...>` | The seek or challenge ended without a game (cancelled, declined, expired). |
| `CHALLENGED <id> <name> <rating> <time> <rated 0/1> <my color w/b/r>` | Incoming challenge. `r` = random color. |
| `CHALLENGE_GONE <id>` | The incoming challenge was withdrawn. |
| `GAME <id> <my color w/b> <white> <white rating> <black> <black rating> <time> <rated 0/1>` | A game is shown. `MOVES`, `STATE` and `LEGAL` follow. |
| `MOVES <first move number> <first color w/b> <san> ...` | The last ~20 moves in SAN, for the move list. |
| `STATE <placement> <turn w/b> <last move or -> <white ms> <black ms> <ply> <check 0/1> <draw offer w/b/->` | Position after every move or draw offer. `placement` is the first FEN field. Clocks are `-1` without a clock. Always followed by `LEGAL`. |
| `LEGAL <uci> <uci> ...` | Legal moves for the DS player. Empty when it's not their turn or the game is over. |
| `END <status> <winner w/b/-> <text...>` | The game ended. `status` is the Lichess status (`mate`, `resign`, `outoftime`, ...). |
| `GONE <seconds>` / `GONE back` | The opponent left. The win can be claimed after `seconds`. / They came back. |
| `CHAT <user> <text...>` | Chat message from the opponent. |

## Example

```
DS> HELLO 1 changeme
 <  WELCOME DSPlayer 1
 <  FRIENDS alice bob
 <  READY
DS> AI 1 10 0 random
 <  INFO Starting a game vs Stockfish level 1
 <  GAME q7ZvsdUF w DSPlayer 1500 Stockfish1 AI 10+0 0
 <  MOVES 1 w
 <  STATE rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - 600000 600000 0 0 -
 <  LEGAL g1h3 g1f3 b1c3 b1a3 h2h3 g2g3 ... e2e4 d2d4 c2c4 b2b4 a2a4
DS> MOVE e2e4
 <  MOVES 1 w e4
 <  STATE rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b e2e4 600000 600000 1 0 -
 <  LEGAL
 <  MOVES 1 w e4 e5
 <  STATE rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w e7e5 600000 600000 2 0 -
 <  LEGAL g1h3 g1f3 ...
```
