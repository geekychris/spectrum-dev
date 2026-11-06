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

# Spectrum Next extra keys: the matrix combo they produce (as on the real
# keyboard) and their bit in NextReg 0xB0 (reg 0) / 0xB1 (reg 1).
NEXT_KEYS = {
    "SEMICOLON": (["SYM", "O"], 0, 7), "QUOTE": (["SYM", "P"], 0, 6),
    "COMMA": (["SYM", "N"], 0, 5), "PERIOD": (["SYM", "M"], 0, 4),
    "CURSOR_UP": (["CAPS", "7"], 0, 3), "CURSOR_DOWN": (["CAPS", "6"], 0, 2),
    "CURSOR_LEFT": (["CAPS", "5"], 0, 1), "CURSOR_RIGHT": (["CAPS", "8"], 0, 0),
    "DELETE": (["CAPS", "0"], 1, 7), "EDIT": (["CAPS", "1"], 1, 6),
    "BREAK": (["CAPS", "SPACE"], 1, 5), "INVVIDEO": (["CAPS", "4"], 1, 4),
    "TRUEVIDEO": (["CAPS", "3"], 1, 3), "GRAPH": (["CAPS", "9"], 1, 2),
    "CAPSLOCK": (["CAPS", "2"], 1, 1), "EXTEND": (["CAPS", "SYM"], 1, 0),
}
NEXT_ALIASES = {"INV_VIDEO": "INVVIDEO", "TRUE_VIDEO": "TRUEVIDEO", "CAPS_LOCK": "CAPSLOCK",
                "DEL": "DELETE", "CUR_UP": "CURSOR_UP", "CUR_DOWN": "CURSOR_DOWN",
                "CUR_LEFT": "CURSOR_LEFT", "CUR_RIGHT": "CURSOR_RIGHT"}

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
    """'A+SPACE', 'up fire', ['Q','O'], 'EDIT' -> canonical names. Joystick names
    get a 'J:' prefix internally ('L' key vs 'LEFT' joystick), Next extra keys 'N:'."""
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
        u = NEXT_ALIASES.get(u, u)
        if u in NEXT_KEYS:
            out.append("N:" + u)
            continue
        u = ALIASES.get(u, u)
        if u not in KEYS:
            raise ValueError(f"unknown key {p!r}: use A-Z 0-9 ENTER SPACE CAPS SYM, joystick "
                             f"UP DOWN LEFT RIGHT FIRE, or Next keys {' '.join(NEXT_KEYS)}")
        out.append(u)
    return out


def ports_hex(pressed) -> str:
    """Canonical names -> the 9 hex bytes for set-ui-io-ports."""
    rows = [0x1F] * 8
    joy = 0
    for k in pressed:
        if k.startswith("J:"):
            joy |= 1 << JOY[k[2:]]
        elif k.startswith("N:"):
            for c in NEXT_KEYS[k[2:]][0]:
                r, b = KEYS[c]
                rows[r] &= ~(1 << b) & 0x1F
        else:
            r, b = KEYS[k]
            rows[r] &= ~(1 << b) & 0x1F
    return "".join(f"{v:02x}" for v in rows) + f"{joy:02x}"


def next_ext(pressed) -> tuple[int, int]:
    """Canonical names -> (NextReg 0xB0, NextReg 0xB1) extended-key bits."""
    regs = [0, 0]
    for k in pressed:
        if k.startswith("N:"):
            _, reg, bit = NEXT_KEYS[k[2:]]
            regs[reg] |= 1 << bit
    return regs[0], regs[1]


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
