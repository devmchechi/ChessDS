"""Bridge settings, read from a KEY=VALUE file and/or environment variables."""

import os
from dataclasses import dataclass, field

DEFAULT_PORT = 5555
KEYS = ("LICHESS_TOKEN", "CHESSDS_SECRET", "CHESSDS_BIND", "CHESSDS_PORT", "CHESSDS_FRIENDS")


class ConfigError(Exception):
    pass


@dataclass
class Config:
    token: str
    secret: str
    bind: str = "0.0.0.0"
    port: int = DEFAULT_PORT
    friends: list = field(default_factory=list)


def read_env_file(path):
    values = {}
    with open(path, encoding="utf-8") as file:
        for raw in file:
            text = raw.strip()
            if not text or text.startswith("#") or "=" not in text:
                continue
            key, value = text.split("=", 1)
            values[key.strip()] = value.strip().strip("\"'")
    return values


def load_config(path=None, environ=os.environ):
    values = {}
    if path is not None:
        if not os.path.exists(path):
            raise ConfigError(f"Config file not found: {path}")
        values.update(read_env_file(path))
    # Environment variables win over the file.
    values.update({key: environ[key] for key in KEYS if key in environ})

    token = values.get("LICHESS_TOKEN", "")
    if not token or token.startswith("lip_xxx"):
        raise ConfigError("Set LICHESS_TOKEN (see bridge.env.example)")
    secret = values.get("CHESSDS_SECRET", "")
    if not secret or any(c.isspace() for c in secret):
        raise ConfigError("Set CHESSDS_SECRET (no spaces)")
    try:
        port = int(values.get("CHESSDS_PORT") or DEFAULT_PORT)
    except ValueError:
        raise ConfigError("CHESSDS_PORT must be a number") from None

    friends = [name.strip() for name in values.get("CHESSDS_FRIENDS", "").split(",") if name.strip()]
    return Config(token, secret, values.get("CHESSDS_BIND") or "0.0.0.0", port, friends)
