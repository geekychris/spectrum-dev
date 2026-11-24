#!/usr/bin/env python3
"""Jump Quest's Next graphics, from the original's own drawing code (the
48K port's tools/gfx_dump.c builds gfx_draw_tile, player_draw, enemies_draw,
items_draw and draw_heart on the host) in the original's 16 colours:

- the palette: the original's 12-bit colours as RGB333 (pen = its colour index;
  pen 0 is the sky, which is transparent on the tilemap and in sprites: the
  fallback colour shows through);
- tiles: each 16x16 level tile as four 8x8 4-bit tilemap tiles (ids 4t..4t+3),
  the HUD's heart (full, empty) and a black tile;
- sprites: 16x16 4-bit patterns: the player (character, facing, legs, top and
  bottom half), the enemies (type, frame), the items, a 2x2 particle.

Writes src/gfx_next.c / .h.  Usage: tools/gen_gfx_next.py
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJ = HERE.parent
ROOT = PROJ.parents[1]
sys.path.insert(0, str(PROJ.parent / "jump_quest" / "tools"))
sys.path.insert(0, str(ROOT / "sdk" / "tools"))
import gen_gfx  # noqa: E402  (the 48K's: dump() and the palette)
from nextpic import rgb9  # noqa: E402

ENEMY_TYPES = [2, 3, 4, 5]
ITEM_TYPES = [10, 11, 12, 13, 14, 20, 21, 22, 23, 24]
RED, DKBROWN, BLACK = 14, 3, 12


def nib(pix, x0, y0, w, h, rows):
    """4-bit pattern bytes: rows of w pixels (pen or None = 0), two a byte"""
    out = []
    for y in range(y0, y0 + rows):
        for x in range(x0, x0 + w, 2):
            def pen(xx):
                if y >= y0 + h or y >= len(pix) or xx >= len(pix[0]):
                    return 0
                c = pix[y][xx]
                return 0 if c is None else c
            out.append(pen(x) << 4 | pen(x + 1))
    return out


def arr(name, data, per=16, typ="uint8_t"):
    lines = ["    " + ", ".join(f"0x{b:02X}" if typ == "uint8_t" else f"0x{b:03X}" for b in data[i:i + per]) + ","
             for i in range(0, len(data), per)]
    return f"const {typ} {name}[{len(data)}] = {{\n" + "\n".join(lines) + "\n};\n"


def main():
    pics = gen_gfx.dump()
    pal = [rgb9(c) for c in gen_gfx.AMIGA]
    tiles = []
    for t in range(16):
        p = pics[f"tile{t}"]
        for cy in (0, 8):
            for cx in (0, 8):
                tiles += nib(p, cx, cy, 8, 8, 8)
    # the HUD's heart: draw_heart(filled) is red, else dark brown (6 x 7)
    for filled in (1, 0):
        h = [[(RED if filled else DKBROWN) if c is not None else BLACK for c in row] for row in pics["heart0"]]
        tiles += nib(h, 0, 0, 8, 8, 8)
    tiles += [BLACK << 4 | BLACK] * 32
    sprites = []
    for ch in range(2):
        for d in range(2):
            for leg in range(3):
                p = pics[f"player{ch}_{d}_{leg}"]
                sprites += nib(p, 0, 0, 16, 28, 16)
                sprites += nib(p, 0, 16, 16, 28, 16)
    for t in ENEMY_TYPES:
        for f in range(2):
            sprites += nib(pics[f"enemy{t}_{f}"], 0, 0, 16, 16, 16)
    for t in ITEM_TYPES:
        sprites += nib(pics[f"item{t}"], 0, 0, 16, 16, 16)
    dot = [[RED, RED] + [None] * 14] * 2 + [[None] * 16] * 14
    sprites += nib(dot, 0, 0, 16, 16, 16)
    npat = len(sprites) // 128
    c = ["/* made by tools/gen_gfx_next.py from the original's drawing code: do not edit */",
         "#include <stdint.h>", '#include "gfx_next.h"', ""]
    c.append("/* the original's 16 colours (gfx.c), RGB333 */")
    c.append(arr("gfx_pal", pal, 8, "uint16_t"))
    c.append("/* tilemap tiles: level tile t's cells 4t (top left) .. 4t+3; then the hearts, black */")
    c.append(arr("gfx_tiles", tiles))
    c.append(f"/* {npat} sprite patterns: see gfx_next.h */")
    c.append(arr("gfx_sprites", sprites))
    (PROJ / "src" / "gfx_next.c").write_text("\n".join(c) + "\n")
    (PROJ / "src" / "gfx_next.h").write_text(f"""/* made by tools/gen_gfx_next.py: do not edit */
#ifndef GFX_NEXT_H
#define GFX_NEXT_H
#include <stdint.h>

extern const uint16_t gfx_pal[16];
#define GFX_T_HEART      64     /* tiles after the level's 64: heart full, empty, black */
#define GFX_T_HEART_EMPTY 65
#define GFX_T_BLACK      66
#define GFX_TILES        67
extern const uint8_t gfx_tiles[GFX_TILES * 32];

/* sprite patterns */
#define GFX_S_PLAYER(ch, left, leg, half) ((((ch) * 2 + (left)) * 3 + (leg)) * 2 + (half))
#define GFX_S_ENEMY(type, frame) (24 + ((type) - 2) * 2 + (frame))   /* ENT_BEETLE.. */
#define GFX_S_ITEM(k)    (32 + (k))                                  /* items_draw's order */
#define GFX_S_DOT        42
#define GFX_SPRITES      {npat}
extern const uint8_t gfx_sprites[GFX_SPRITES * 128];
#endif
""")
    print(f"gen_gfx_next: {len(tiles) // 32} tiles, {npat} sprite patterns")


if __name__ == "__main__":
    sys.exit(main())
