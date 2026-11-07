#!/usr/bin/env python3
"""Orbit Docking's sprite patterns for the Next (8-bit, 256 bytes each,
transparent 0xE3), loaded once at start-up from bank SPR_BANK:

   0..8    the dial (redrawn by hud.c when the needle moves; blank here)
   9..17   the axis box (render.c hud(): a box, its two lines through the middle)
   18, 19  the axis box's dot, green and red
   20      a life (a small cyan bar)
   21..    the big letters: the original's 16 x 16 font (amiga3d
           a3d_font16.h), pen 1 (each ink is a sprite palette offset)

Output: gfx/bank_22.bin, src/gfx_data.asm, src/gfx_data.h

    python3 tools/gen_gfx.py
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
SPR_BANK = 22
GLYPHS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!"
T = 0xE3
# sprite palette (hud.c sets these): 1 + 16 k the inks, and in offset 0:
PAL_PANEL, PAL_GREY, PAL_GREEN, PAL_RED, PAL_CYAN = 2, 3, 4, 5, 6


def font16():
    src = open(os.path.join(HERE, "orig", "amiga3d", "a3d_font16.h")).read()
    rows = re.findall(r"\{ (0x[0-9A-F]{4}(?:, 0x[0-9A-F]{4}){15}) \}", src)
    assert len(rows) == 95
    return [[int(v, 16) for v in r.split(", ")] for r in rows]


def blank():
    return bytearray([T] * 256)


def axis_box():
    """render.c hud(): g_box(W-52, 4, W-4, 52, 2, 3), lines at x W-28 (6..50)
    and y 28 (W-50..W-6); the sprites start at (268, 4): 48 x 48 (port: the
    box's last row and column are off the 3 x 3 sprites)"""
    img = [[T] * 48 for _ in range(48)]
    for y in range(48):
        for x in range(48):
            edge = x == 0 or y == 0 or x == 47 or y == 47
            img[y][x] = PAL_GREY if edge else PAL_PANEL
    for y in range(2, 47):
        img[y][24] = PAL_GREY
    for x in range(2, 47):
        img[24][x] = PAL_GREY
    pats = []
    for ty in range(3):
        for tx in range(3):
            p = blank()
            for y in range(16):
                for x in range(16):
                    p[y * 16 + x] = img[ty * 16 + y][tx * 16 + x]
            pats.append(p)
    return pats


def square(w, h, pen):
    p = blank()
    for y in range(h):
        for x in range(w):
            p[y * 16 + x] = pen
    return p


def glyph(rows):
    p = blank()
    for y in range(16):
        for x in range(16):
            if rows[y] & (0x8000 >> x):
                p[y * 16 + x] = 1
    return p


def main():
    f = font16()
    pats = [blank() for _ in range(9)] + axis_box() + [square(5, 5, PAL_GREEN), square(5, 5, PAL_RED),
                                                        square(7, 5, PAL_CYAN)]
    first_glyph = len(pats)
    for c in GLYPHS:
        pats.append(glyph(f[ord(c) - 32]))
    assert len(pats) <= 64
    data = b"".join(pats)
    data += bytes(16384 - len(data))
    os.makedirs(os.path.join(PROJ, "gfx"), exist_ok=True)
    with open(os.path.join(PROJ, "gfx", f"bank_{SPR_BANK}.bin"), "wb") as fh:
        fh.write(data)
    with open(os.path.join(PROJ, "src", "gfx_data.asm"), "w") as fh:
        fh.write(f"; made by tools/gen_gfx.py - do not edit: the sprite patterns\n\n"
                 f"SECTION BANK_{SPR_BANK}\nBINARY \"gfx/bank_{SPR_BANK}.bin\"\n")
    with open(os.path.join(PROJ, "src", "gfx_data.h"), "w") as fh:
        fh.write(f"""/* made by tools/gen_gfx.py - do not edit: the sprite patterns (see there) */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#define SPR_BANK     {SPR_BANK}
#define SPR_NPAT     {len(pats)}
#define PAT_DIAL     0
#define PAT_AXIS     9
#define PAT_DOT_OK   18
#define PAT_DOT_BAD  19
#define PAT_LIFE     20
#define PAT_GLYPH    {first_glyph}
#define SPR_GLYPHS   "{GLYPHS}"
#define PAL_PANEL    {PAL_PANEL}
#define PAL_GREY     {PAL_GREY}
#define PAL_GREEN    {PAL_GREEN}
#define PAL_RED      {PAL_RED}
#define PAL_CYAN     {PAL_CYAN}
#endif
""")
    print(f"{len(pats)} patterns in bank {SPR_BANK}")


if __name__ == "__main__":
    main()
