"""Spectrum keyboard matrix + joystick, as ZRCP's set-ui-io-ports expects it.

Eight half-rows (ports xxFE), five keys each; 0 = pressed. Plus one joystick
byte (1 = pressed) that ZEsarUX maps onto the emulated joystick type
(Kempston by default in our sessions; Sinclair 1/2, Cursor... via
zxh-joystick).
"""
from __future__ import annotations

# rows in set-ui-io-ports order; within a row, bit 0 first
ROWS = [
    ("FEFE", ["CAPS", "Z", "X", "C", "V"]),
    ("FDFE", ["A", "S", "D", "F", "G"]),
    ("FBFE", ["Q", "W", "E", "R", "T"]),
    ("F7FE", ["1", "2", "3", "4", "5"]),
    ("EFFE", ["0", "9", "8", "7", "6"]),
    ("DFFE", ["P", "O", "I", "U", "Y"]),
    ("BFFE", ["ENTER", "L", "K", "J", "H"]),
    ("7FFE", ["SPACE", "SYM", "M", "N", "B"]),
]

KEYS: dict[str, tuple[int, int]] = {}
for _r, (_port, _names) in enumerate(ROWS):
    for _b, _n in enumerate(_names):
        KEYS[_n] = (_r, _b)

ALIASES = {
    "SHIFT": "CAPS", "CAPSSHIFT": "CAPS", "CS": "CAPS",
    "SYMBOL": "SYM", "SYMSHIFT": "SYM", "SS": "SYM",
    "RETURN": "ENTER", "CR": "ENTER", "SPC": "SPACE", " ": "SPACE",
}

JOY = {"RIGHT": 0, "LEFT": 1, "DOWN": 2, "UP": 3, "FIRE": 4}
JOY_ALIASES = {"R": "RIGHT", "L": "LEFT", "D": "DOWN", "U": "UP", "F": "FIRE", "BUTTON": "FIRE"}

# Shifted characters for type(): char -> keys pressed together
SYMBOLS = {
    '"': ["SYM", "P"], "!": ["SYM", "1"], "@": ["SYM", "2"], "#": ["SYM", "3"],
    "$": ["SYM", "4"], "%": ["SYM", "5"], "&": ["SYM", "6"], "'": ["SYM", "7"],
    "(": ["SYM", "8"], ")": ["SYM", "9"], "_": ["SYM", "0"], "<": ["SYM", "R"],
    ">": ["SYM", "T"], ";": ["SYM", "O"], ":": ["SYM", "Z"], "?": ["SYM", "C"],
    "/": ["SYM", "V"], "*": ["SYM", "B"], ",": ["SYM", "N"], ".": ["SYM", "M"],
    "+": ["SYM", "K"], "-": ["SYM", "J"], "=": ["SYM", "L"], "^": ["SYM", "H"],
    "\n": ["ENTER"],
}


def parse(spec) -> list[str]:
    """'A+SPACE', 'up fire', ['Q','O'] -> canonical names. Joystick names keep a
    'J:' prefix internally so 'L' (key) and 'LEFT' (joystick) can't clash."""
    if isinstance(spec, str):
        parts = spec.replace("+", " ").replace(",", " ").split()
    else:
        parts = list(spec)
    out = []
    for p in parts:
        u = str(p).upper()
        if u.startswith("J:"):
            u = u[2:]
            u = JOY_ALIASES.get(u, u)
            if u not in JOY:
                raise ValueError(f"unknown joystick input {p!r} (UP DOWN LEFT RIGHT FIRE)")
            out.append("J:" + u)
            continue
        if u in JOY:
            out.append("J:" + u)
            continue
        u = ALIASES.get(u, u)
        if u not in KEYS:
            raise ValueError(f"unknown key {p!r}: use A-Z 0-9 ENTER SPACE CAPS SYM, "
                             "or joystick UP DOWN LEFT RIGHT FIRE")
        out.append(u)
    return out


def ports_hex(pressed) -> str:
    """Canonical names -> the 9 hex bytes for set-ui-io-ports."""
    rows = [0x1F] * 8
    joy = 0
    for k in pressed:
        if k.startswith("J:"):
            joy |= 1 << JOY[k[2:]]
        else:
            r, b = KEYS[k]
            rows[r] &= ~(1 << b) & 0x1F
    return "".join(f"{v:02x}" for v in rows) + f"{joy:02x}"


def chord_for_char(ch: str) -> list[str]:
    """Keys for one typed character (letters type in lower case unless upper)."""
    if ch in SYMBOLS:
        return SYMBOLS[ch]
    if ch == " ":
        return ["SPACE"]
    if ch.isalpha() and ch.upper() in KEYS:
        return ["CAPS", ch.upper()] if ch.isupper() else [ch.upper()]
    if ch.isdigit():
        return [ch]
    raise ValueError(f"can't type {ch!r}")
