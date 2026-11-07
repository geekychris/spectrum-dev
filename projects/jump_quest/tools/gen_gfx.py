#!/usr/bin/env python3
"""Jump Quest's Spectrum graphics from the original's own drawing code.

Builds tools/gfx_dump.c (the original's gfx_draw_tile, player_draw,
enemies_draw, items_draw and draw_heart, verbatim in tools/amiga_draw.c, over
a host RastPort) and converts what it draws:

- tiles: each 16x16 tile is four character cells; per cell the paper is the
  sky where there is sky (so the sky is one colour everywhere), otherwise its
  most common colour, the ink its next most common colour of another
  Spectrum colour, and every pixel goes to the nearer of the two;
- items: the same, as four cells over the sky (port: they are tiles);
- sprites: 1 bit a pixel (drawn = ink) except a few "hole" colours per
  sprite (eyes, highlights, stitching), which show the paper behind, so the
  details survive in one colour; each sprite has one ink (cellspr48 colours
  the cells it covers).

Writes src/gfx_data.c and src/gfx_data.h.  Usage: tools/gen_gfx.py
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJ = HERE.parent
BUILD = PROJ / "build"

# the original's 16 colours (gfx.c palette, 4 bits a gun)
AMIGA = [0x59C, 0x47A, 0xA62, 0x741, 0x4A2, 0x381, 0xD83, 0xA52,
         0xEC9, 0x999, 0x35A, 0xEEE, 0x111, 0xDD3, 0xD22, 0x4D4]
SKY, SKY2, BROWN, DKBROWN, GREEN, DKGREEN, ORANGE, DKORANGE, SKIN, GREY, BLUE, WHITE, \
    BLACK, YELLOW, RED, LTGREEN = range(16)
# Spectrum colour (0-7) and bright for each, chosen by eye
ZX_OF = {SKY: (5, 0), SKY2: (1, 0), BROWN: (2, 0), DKBROWN: (0, 0), GREEN: (4, 1),
         DKGREEN: (4, 0), ORANGE: (2, 1), DKORANGE: (2, 0), SKIN: (6, 1), GREY: (7, 0),
         BLUE: (1, 1), WHITE: (7, 1), BLACK: (0, 0), YELLOW: (6, 1), RED: (2, 1),
         LTGREEN: (4, 1)}
INK = dict(black=0, blue=1, red=2, magenta=3, green=4, cyan=5, yellow=6, white=7)


def rgb_amiga(c):
    v = AMIGA[c]
    return ((v >> 8) & 15) * 17, ((v >> 4) & 15) * 17, (v & 15) * 17


def rgb_zx(z, bright):
    lv = 255 if bright else 205
    return (lv if z & 2 else 0, lv if z & 4 else 0, lv if z & 1 else 0)


def dist(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b))


def dump():
    BUILD.mkdir(exist_ok=True)
    exe = BUILD / "gfx_dump"
    subprocess.run(["cc", "-O0", "-w", "-I", str(HERE), "-o", str(exe), str(HERE / "gfx_dump.c")], check=True)
    out = subprocess.run([str(exe)], check=True, capture_output=True, text=True).stdout.splitlines()
    pics, i = {}, 0
    while i < len(out):
        name, w, h = out[i].split()
        w, h = int(w), int(h)
        pics[name] = [[None if ch == "." else int(ch, 16) for ch in row] for row in out[i + 1:i + 1 + h]]
        i += 1 + h
    return pics


def cell(pix):
    """an 8x8 block of pens -> (8 bytes, attribute)"""
    counts = {}
    for row in pix:
        for c in row:
            counts[c] = counts.get(c, 0) + 1
    order = sorted(counts, key=lambda c: -counts[c])
    paper = SKY if SKY in counts else order[0]
    pz, pb = ZX_OF[paper]
    bright = 0 if paper == SKY else pb
    # the ink: the most common other colour, black only if nothing else (a
    # ribbon or a highlight says more than an outline)
    inks = [c for c in order if ZX_OF[c][0] != pz]
    inks.sort(key=lambda c: ZX_OF[c][0] == 0)
    shade = not inks and len(order) > 1     # only shades of the paper (brick mortar)
    iz = ZX_OF[inks[0]][0] if inks else (0 if pz else 7)
    prgb, irgb = rgb_zx(pz, bright), rgb_zx(iz, bright)
    data = []
    for row in pix:
        b = 0
        for x, c in enumerate(row):
            z = ZX_OF[c][0]
            if shade:
                on = c != paper                 # the other shade, in black
            else:
                on = z == iz if z in (iz, pz) else dist(rgb_amiga(c), irgb) < dist(rgb_amiga(c), prgb)
            if on:
                b |= 0x80 >> x
        data.append(b)
    return data, (bright << 6) | (pz << 3) | iz


def bits(pic, holes=()):
    """sprite rows -> bytes, 2 a row (16 pixels)"""
    out = []
    for row in pic:
        v = 0
        for x, c in enumerate(row):
            if c is not None and c not in holes:
                v |= 0x8000 >> x
        out += [v >> 8, v & 0xFF]
    return out


def bits8(pic, holes=()):
    out = []
    for row in pic:
        v = 0
        for x, c in enumerate(row):
            if c is not None and c not in holes:
                v |= 0x80 >> x
        out.append(v)
    return out


ENEMIES = [  # type, ink, hole colours
    (2, "yellow", {WHITE}),          # beetle (brown): its eyes
    (3, "white", {RED}),             # fly (grey, white wings): its eyes
    (4, "black", {RED}),             # spider: its eyes
    (5, "red", set()),               # ant
]
ITEMS = [
    (10, "green", {YELLOW}),         # circuit board: the traces
    (11, "blue", {GREY, BLACK}),     # diskette: the slider and the hub
    (12, "red", {WHITE, YELLOW}),    # ball: highlight, stripe
    (13, "white", {GREY}),           # pillow: stitching
    (14, "green", set()),            # plant
    (20, "yellow", {YELLOW, WHITE}), # briefcase (brown): clasp, shine
    (21, "red", {WHITE}),            # heart: highlight
    (22, "white", {BLUE}),           # pillow power-up: ZZ
    (23, "yellow", set()),           # beer
    (24, "magenta", {WHITE}),        # wine: highlight
]


def arr(name, data, per=16):
    lines = []
    for i in range(0, len(data), per):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + per]) + ",")
    return f"const uint8_t {name}[{len(data)}] = {{\n" + "\n".join(lines) + "\n};\n"


def main():
    pics = dump()
    tiles, attrs = [], []
    for t in range(16):
        p = pics[f"tile{t}"]
        for cy in (0, 8):
            for cx in (0, 8):
                d, a = cell([row[cx:cx + 8] for row in p[cy:cy + 8]])
                tiles += d
                attrs.append(a)
    player = []
    for ch in range(2):
        for d in range(2):
            for leg in range(3):
                player += bits(pics[f"player{ch}_{d}_{leg}"], {BLACK})
    enemies = []
    for t, _, holes in ENEMIES:
        for f in range(2):
            enemies += bits(pics[f"enemy{t}_{f}"], holes)
    items = []
    for t, ink, holes in ITEMS:
        items += bits(pics[f"item{t}"], holes)
    # port: items are tiles (cells 64.. : 4 an item, over the sky, in their ink)
    item_cells, item_attrs = [], []
    for k, (t, ink, holes) in enumerate(ITEMS):
        rows = items[k * 32:(k + 1) * 32]
        for cy in (0, 8):
            for cx in (0, 1):
                item_cells += [rows[(cy + y) * 2 + cx] for y in range(8)]
                item_attrs.append((ZX_OF[SKY][0] << 3) | INK[ink])
    hearts = bits8(pics["heart0"]) + bits8(pics["heart1"])

    c = ["/* made by tools/gen_gfx.py from the original's drawing code: do not edit */",
         "#include <stdint.h>", '#include "gfx_data.h"', ""]
    c.append("/* tile t's cells: ids 4t (top left), 4t+1, 4t+2, 4t+3 (bottom right);")
    c.append("   then the items' (GFX_ITEM_CELLS + 4k for item k, over the sky) */")
    c.append(arr("gfx_tile_cells", tiles + item_cells))
    c.append(arr("gfx_tile_attr", attrs + item_attrs))
    c.append("/* [character][facing right, left][legs 0, +2, -2]: 28 lines of 2 bytes */")
    c.append(arr("gfx_player", player))
    c.append("/* beetle, fly, spider, ant: [type][frame]: 16 lines of 2 bytes */")
    c.append(arr("gfx_enemy", enemies))
    c.append("const uint8_t gfx_enemy_ink[4] = { " + ", ".join(f"INK_{i.upper()}" for _, i, _ in ENEMIES) + " };")
    c.append("/* the HUD's heart, empty and full */")
    c.append(arr("gfx_heart", hearts, 8))
    (PROJ / "src" / "gfx_data.c").write_text("\n".join(c) + "\n")
    h = """/* made by tools/gen_gfx.py: do not edit */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>
#include <arch/zx.h>

#define GFX_ITEM_CELLS 64          /* item k's cells: 64 + 4k .. 64 + 4k + 3 */
extern const uint8_t gfx_tile_cells[(64 + 40) * 8];
extern const uint8_t gfx_tile_attr[64 + 40];
#define GFX_PLAYER_H 28
extern const uint8_t gfx_player[2 * 2 * 3 * 28 * 2];
extern const uint8_t gfx_enemy[4 * 2 * 16 * 2];
extern const uint8_t gfx_enemy_ink[4];
extern const uint8_t gfx_heart[16];

#endif
"""
    (PROJ / "src" / "gfx_data.h").write_text(h)
    print(f"gen_gfx: {len(tiles)} tile bytes, {len(player)} player, {len(enemies)} enemy, {len(items)} item")


if __name__ == "__main__":
    sys.exit(main())
