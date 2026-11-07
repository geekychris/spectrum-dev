#!/usr/bin/env python3
"""Drone Courier's graphics for the Next, from the original's render.c,
game.c (plat_sky) and the amiga3d engine's 16 x 16 font:

  src/gfx_data.c/.h   the Layer 2 palette (the original's 32 pens, then its
                      sky gradient's 16 steps), the sprite glyph list
  gfx/bank_24.bin     page 48: the projection's reciprocals, FOV * 4096 / z
                      (uint16, z = 0..4095; view.c maps it while it builds
                      the polygon list)
  gfx/bank_23.bin     sprite patterns (8 bpp, 0xE3 transparent):
                      0..15   the arrow to the next pad, 16 headings
                      16      the wind gauge (redrawn by the game)
                      17..    the 16 x 16 font's glyphs (GLYPHS)

Run from anywhere: python3 tools/gen_gfx.py (the output is checked in).
"""
import math
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
A3D = os.environ.get("A3D", "/Users/chris/code/claude_world/amiga_mcp/examples/amiga3d")
SPR_BANK = 23
RECIP_BANK = 24                  # page 48: FOV * 4096 / z for z = 0..4095 (view.c project)
FOV = 160
GLYPHS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'!"
P_ARROW, P_WIND, P_GLYPH = 0, 16, 17
TR = 0xE3
INK = 1          # sprite pixel for ink (palette offset k: index 1 + 16k)

# render.c courier_palette (12-bit)
PAL = [0x000, 0xFFF, 0x123, 0x99A, 0x7AE, 0x667, 0x5A5, 0xCBA, 0x987, 0x9AB, 0x678, 0xBA9, 0x876,
       0x556, 0x39F, 0x3E6, 0xEF8, 0xEEE, 0x999, 0x444, 0xB85, 0xE44, 0x333, 0x000, 0x000, 0x000,
       0xF33, 0xFE4, 0x6F6, 0x5EF, 0x000, 0x000]
SKY_TOP, SKY_BOT, SKY_STEPS = 0x48C, 0xCDE, 16     # game.c: plat_sky(4, 0x48C, 0xCDE, 120)


def mix(a, b, k, n):
    """main_68k.c mix(): channel by channel, a + (b - a) * k / n"""
    out = 0
    for sh in (8, 4, 0):
        ca, cb = (a >> sh) & 15, (b >> sh) & 15
        out |= (ca + (cb - ca) * k // n) << sh
    return out


def rgb9(c12):
    """12-bit 0xRGB -> the Next's RGB333 (nearest of 8 levels)"""
    r, g, b = (c12 >> 8) & 15, (c12 >> 4) & 15, c12 & 15
    q = lambda v: (v * 7 + 7) // 15
    return q(r) << 6 | q(g) << 3 | q(b)


def font16():
    src = open(os.path.join(A3D, "a3d_font16.h")).read()
    rows = re.findall(r"\{((?:\s*0x[0-9A-Fa-f]{4},?){16})\s*\}", src)
    assert len(rows) == 95, len(rows)
    return [[int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{4}", r)] for r in rows]


def glyph_pattern(g):
    pat = bytearray([TR]) * 256
    for y in range(16):
        for x in range(16):
            if g[y] & (0x8000 >> x):
                pat[y * 16 + x] = INK
    return pat


def arrow_pattern(k):
    """render.c hud(): a triangle, tip 14 out at the heading, the back corners
    9 out at +-26000/65536 of a turn; here at half size, shown at zoom x2"""
    rel = k * 4096
    def pt(a, r):
        a = a * 2 * math.pi / 65536
        return 7.5 + math.sin(a) * r / 2, 7.5 - math.cos(a) * r / 2
    tri = [pt(rel, 14), pt(rel + 26000, 9), pt(rel - 26000, 9)]
    pat = bytearray([TR]) * 256
    for y in range(16):
        for x in range(16):
            # inside test at the pixel centre (either winding)
            px, py = x + 0.5 - 0.5, y + 0.5 - 0.5
            s = []
            for i in range(3):
                (x0, y0), (x1, y1) = tri[i], tri[(i + 1) % 3]
                s.append((x1 - x0) * (py - y0) - (y1 - y0) * (px - x0))
            if all(v >= 0 for v in s) or all(v <= 0 for v in s):
                pat[y * 16 + x] = INK
    return pat


def main():
    f16 = font16()
    pats = [arrow_pattern(k) for k in range(16)]
    pats.append(bytearray([TR]) * 256)                      # the wind gauge (drawn at run time)
    for ch in GLYPHS:
        pats.append(glyph_pattern(f16[ord(ch) - 32]))
    assert len(pats) <= 64
    data = b"".join(pats).ljust(16384, b"\0")
    with open(os.path.join(PROJ, "gfx", f"bank_{SPR_BANK}.bin"), "wb") as f:
        f.write(data)
    rec = bytearray()
    for z in range(4096):
        rec += min(65535, FOV * 4096 // z if z else 65535).to_bytes(2, "little")
    with open(os.path.join(PROJ, "gfx", f"bank_{RECIP_BANK}.bin"), "wb") as f:
        f.write(bytes(rec).ljust(16384, b"\0"))
    pal = [rgb9(c) for c in PAL]
    sky = [rgb9(mix(SKY_TOP, SKY_BOT, k, SKY_STEPS - 1)) for k in range(SKY_STEPS)]
    out = ["/* made by tools/gen_gfx.py from the original's render.c and game.c: do not edit */",
           '#include "gfx_data.h"', "",
           "/* Layer 2 0..31: render.c courier_palette; 32..47: plat_sky's 16 steps */",
           "const uint16_t gfx_palette[48] = {"]
    allp = pal + sky
    for i in range(0, 48, 12):
        out.append("    " + ", ".join(f"0x{c:03X}" for c in allp[i:i + 12]) + ",")
    out.append("};")
    with open(os.path.join(PROJ, "src", "gfx_data.c"), "w") as f:
        f.write("\n".join(out) + "\n")
    with open(os.path.join(PROJ, "src", "gfx_data.h"), "w") as f:
        f.write(f'''/* made by tools/gen_gfx.py (see there) */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>
#define SPR_BANK   {SPR_BANK}           /* sprite patterns, loaded by the .nex */
#define RECIP_PAGE {RECIP_BANK * 2}           /* FOV * 4096 / z, z < 4096 (uint16 at 0x6000 when mapped there) */
#define SPR_NPAT   {len(pats)}
#define P_ARROW    {P_ARROW}            /* 16 headings, 0 = up, clockwise */
#define P_WIND     {P_WIND}
#define P_GLYPH    {P_GLYPH}
#define SPR_GLYPHS "{GLYPHS}"
#define SPR_INK    {INK}            /* a pattern's ink pixel (+16 x the palette offset) */
#define PAL_SKY    32           /* Layer 2: the sky's 16 steps */
extern const uint16_t gfx_palette[48];
#endif
''')
    with open(os.path.join(PROJ, "src", "gfx_pages.asm"), "w") as f:
        f.write(f'; made by tools/gen_gfx.py: the sprite patterns, into bank {SPR_BANK} by the .nex\n'
                f'SECTION BANK_{SPR_BANK}\nBINARY "gfx/bank_{SPR_BANK}.bin"\n'
                f'SECTION BANK_{RECIP_BANK}\nBINARY "gfx/bank_{RECIP_BANK}.bin"\n')
    print(f"{len(pats)} patterns; sky {[hex(c) for c in sky]}")


if __name__ == "__main__":
    main()
