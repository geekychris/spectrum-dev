#!/usr/bin/env python3
"""Text -> tokenised (Next)BASIC programs and +3DOS files, for files on an SD card.

    prog = tokenize('''
    10 CLEAR 32766: LOAD "c:/arcade/menu.bin" CODE 32768
    20 RANDOMIZE USR 32768: GO TO 1000+10*PEEK 32767
    1000 LAYER CLEAR: CLS: .nexload c:/arcade/game.nex
    ''')
    open("autoexec.bas", "wb").write(plus3dos_basic(prog, autostart=10))
    open("menu.bin", "wb").write(plus3dos_code(code, 32768))

Keywords must be written in upper case (multi-word ones with their space:
"GO TO", "DEF FN"). Numbers get their hidden 5-byte form (integers 0..65535).
A statement that starts with "." is a dot command and is kept as text up to
the next ":" outside quotes, as NextZXOS stores it. Only the NextBASIC keywords
in NEXT_TOKENS are known (LAYER, PALETTE, SPRITE; checked against the
distribution's own .bas files); 128K BASIC's SPECTRUM (0xA3), which NextBASIC
uses to load snapshots, and PLAY are there with the 48K ones.
"""
import struct

# 0xA3.. 0xFF: the 128K + 48K keywords
_ROM = ["SPECTRUM", "PLAY", "RND", "INKEY$", "PI", "FN", "POINT", "SCREEN$", "ATTR", "AT", "TAB",
        "VAL$", "CODE", "VAL", "LEN", "SIN", "COS", "TAN", "ASN", "ACS", "ATN", "LN", "EXP", "INT",
        "SQR", "SGN", "ABS", "PEEK", "IN", "USR", "STR$", "CHR$", "NOT", "BIN", "OR", "AND", "<=",
        ">=", "<>", "LINE", "THEN", "TO", "STEP", "DEF FN", "CAT", "FORMAT", "MOVE", "ERASE",
        "OPEN #", "CLOSE #", "MERGE", "VERIFY", "BEEP", "CIRCLE", "INK", "PAPER", "FLASH", "BRIGHT",
        "INVERSE", "OVER", "OUT", "LPRINT", "LLIST", "STOP", "READ", "DATA", "RESTORE", "NEW",
        "BORDER", "CONTINUE", "DIM", "REM", "FOR", "GO TO", "GO SUB", "INPUT", "LOAD", "LIST", "LET",
        "PAUSE", "NEXT", "POKE", "PRINT", "PLOT", "RUN", "SAVE", "RANDOMIZE", "IF", "CLS", "DRAW",
        "CLEAR", "RETURN", "COPY"]
TOKENS = {kw: 0xA3 + i for i, kw in enumerate(_ROM)}
NEXT_TOKENS = {"LAYER": 0x9C, "PALETTE": 0x9D, "SPRITE": 0x9E}
TOKENS.update(NEXT_TOKENS)
_BY_LENGTH = sorted(TOKENS, key=len, reverse=True)


def number5(v: int) -> bytes:
    """The 5-byte form of a small integer."""
    if not (isinstance(v, int) and 0 <= v <= 65535):
        raise ValueError(f"only integers 0..65535 are supported, not {v!r}")
    return bytes([0x0E, 0, 0]) + struct.pack("<H", v) + b"\0"


def _line(text: str) -> bytes:
    out = bytearray()
    i, n = 0, len(text)
    stmt_start = True
    while i < n:
        c = text[i]
        if stmt_start and c == " ":
            i += 1
            continue
        if stmt_start and c == ".":                # dot command: text up to ':'
            j, q = i, False
            while j < n and (q or text[j] != ":"):
                q ^= text[j] == '"'
                j += 1
            out += text[i:j].rstrip().encode("latin-1")
            i = j
            continue
        stmt_start = False
        if c == '"':
            j = text.index('"', i + 1)
            out += text[i:j + 1].encode("latin-1")
            i = j + 1
            continue
        if c == ":":
            out += b":"
            i += 1
            stmt_start = True
            continue
        if c.isdigit():
            j = i
            while j < n and text[j].isdigit():
                j += 1
            out += text[i:j].encode() + number5(int(text[i:j]))
            i = j
            continue
        for kw in _BY_LENGTH:
            if text.startswith(kw, i):
                end = i + len(kw)
                alpha = kw[-1].isalpha()
                if alpha and end < n and (text[end].isalnum() or text[end] == "$"):
                    continue
                if kw[0].isalpha() and i > 0 and text[i - 1].isalnum():
                    continue
                out.append(TOKENS[kw])
                i = end
                while i < n and text[i] == " ":     # the token carries its spaces
                    i += 1
                if kw == "REM":
                    out += text[i:].encode("latin-1")
                    i = n
                if TOKENS[kw] == TOKENS["THEN"]:
                    stmt_start = True
                break
        else:
            if c == " ":
                i += 1                              # spaces between items aren't stored
                continue
            out += c.encode("latin-1")
            i += 1
    return bytes(out)


def tokenize(source: str) -> bytes:
    """A whole program: one "NUMBER statements" line per text line."""
    prog = bytearray()
    for raw in source.strip().splitlines():
        raw = raw.strip()
        if not raw:
            continue
        num, _, rest = raw.partition(" ")
        body = _line(rest) + b"\r"
        prog += struct.pack(">H", int(num)) + struct.pack("<H", len(body)) + body
    return bytes(prog)


def plus3dos(data: bytes, kind: int, p1: int, p2: int) -> bytes:
    """A +3DOS file: the 128-byte header (kind 0 = program, 3 = code) + data."""
    h = bytearray(128)
    h[0:8] = b"PLUS3DOS"
    h[8], h[9], h[10] = 0x1A, 1, 0
    struct.pack_into("<I", h, 11, 128 + len(data))
    h[15] = kind
    struct.pack_into("<HHH", h, 16, len(data), p1, p2)
    h[127] = sum(h[:127]) & 0xFF
    return bytes(h) + data


def plus3dos_basic(prog: bytes, autostart: int | None = None) -> bytes:
    return plus3dos(prog, 0, 0x8000 if autostart is None else autostart, len(prog))


def plus3dos_code(code: bytes, addr: int) -> bytes:
    return plus3dos(code, 3, addr, 0x8000)
