"""The line protocol spoken between the Nintendo DS and the bridge.

Every message is one line of printable ASCII ending in "\\n". Fields are
separated by spaces; only the last field of a message may contain spaces.
See docs/PROTOCOL.md for the list of messages.
"""

import re
import unicodedata

PROTOCOL_VERSION = 1
MAX_LINE = 4096

_NOT_PRINTABLE = re.compile(r"[^\x20-\x7e]")


def ascii_text(text, limit=200):
    """Printable ASCII version of text on a single line (the DS font is ASCII)."""
    text = unicodedata.normalize("NFKD", str(text)).encode("ascii", "ignore").decode("ascii")
    text = " ".join(_NOT_PRINTABLE.sub(" ", text).split())
    return text[:limit]


def token(value):
    """A single field: no spaces and never empty."""
    return ascii_text(value, limit=64).replace(" ", "_") or "-"


def line(command, *fields, text=None):
    """Builds a message. `text`, if given, is a free-text last field."""
    parts = [command, *(token(field) for field in fields)]
    if text is not None:
        parts.append(ascii_text(text))
    return " ".join(parts)


def parse(raw):
    """Splits a received line into (COMMAND, [arguments])."""
    parts = raw.split()
    if not parts:
        return None, []
    return parts[0].upper(), parts[1:]
