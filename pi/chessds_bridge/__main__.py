"""Run the bridge: python -m chessds_bridge [--config bridge.env] [-v]"""

import argparse
import logging
import os
import sys

from .bridge import Bridge
from .config import ConfigError, load_config
from .lichess import Lichess, LichessError
from .server import Server


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="chessds_bridge",
        description="Lets the ChessDS Nintendo DS client play on Lichess.",
    )
    parser.add_argument("--config", help="KEY=VALUE settings file (default: ./bridge.env if present)")
    parser.add_argument("-v", "--verbose", action="store_true", help="log every protocol line")
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    # requests/urllib3 are noisy at DEBUG level.
    logging.getLogger("urllib3").setLevel(logging.INFO)

    path = args.config or os.environ.get("CHESSDS_CONFIG")
    if path is None and os.path.exists("bridge.env"):
        path = "bridge.env"
    try:
        config = load_config(path)
    except ConfigError as e:
        logging.error("%s", e)
        return 2
    if config.secret == "changeme":
        logging.warning("CHESSDS_SECRET is still 'changeme'; pick your own (and put it in chessds.ini)")

    bridge = Bridge(Lichess(config.token), config.secret, config.friends)
    try:
        bridge.start()
    except LichessError as e:
        logging.error("Can't log in to Lichess: %s (check LICHESS_TOKEN)", e)
        return 1

    try:
        Server(bridge, config.bind, config.port).serve_forever()
    except KeyboardInterrupt:
        pass
    except OSError as e:
        logging.error("Can't listen on %s:%d: %s", config.bind, config.port, e)
        return 1
    finally:
        bridge.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
