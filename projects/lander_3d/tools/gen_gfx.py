#!/usr/bin/env python3
"""Lander 3D's sprite patterns (8-bit, 256 bytes each, transparent 0xE3),
loaded at start-up from bank SPR_BANK: the big letters, the original's
16 x 16 font (amiga3d a3d_font16.h), pen 1 (each ink a sprite palette
offset). Run by the Makefile:

    python3 tools/gen_gfx.py OUTDIR       -> OUTDIR/sprites.bin, OUTDIR/gfx_data.h
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SPR_BANK = 25
GLYPHS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!+x.,-"
T = 0xE3


def font16():
    src = (HERE / "orig" / "amiga3d" / "a3d_font16.h").read_text()
    rows = re.findall(r"\{ (0x[0-9A-F]{4}(?:, 0x[0-9A-F]{4}){15}) \}", src)
    assert len(rows) == 95
    return [[int(v, 16) for v in r.split(", ")] for r in rows]


def glyph(rows):
    p = bytearray([T] * 256)
    for y in range(16):
        for x in range(16):
            if rows[y] & (0x8000 >> x):
                p[y * 16 + x] = 1
    return p


def main():
    out = Path(sys.argv[1])
    f = font16()
    pats = [glyph(f[ord(c) - 32]) for c in GLYPHS]
    assert len(pats) <= 64
    data = b"".join(pats)
    (out / "sprites.bin").write_bytes(data + bytes(16384 - len(data)))
    (out / "gfx_data.h").write_text(f"""/* made by tools/gen_gfx.py (the Makefile) - the sprite patterns */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#define SPR_BANK     {SPR_BANK}
#define SPR_NPAT     {len(pats)}
#define PAT_GLYPH    0
#define SPR_GLYPHS   "{GLYPHS}"
#endif
""")


if __name__ == "__main__":
    main()
