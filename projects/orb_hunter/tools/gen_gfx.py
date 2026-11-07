#!/usr/bin/env python3
"""Orb Hunter's pictures for the Spectrum Next, drawn on the host.

The sprites are the original's own drawing (draw.c: draw_hunter_standing,
draw_enemy_crawler ... draw_bombs, in its 16-colour palette, rectangle for
rectangle) as 4-bit 16 x 16 patterns for sdk/include/nextsprl.h; the
bosses are 2 x 2 patterns. The tiles keep the original's designs (rock with
its specks, bricks with their mortar, bevelled metal, one-way ledges,
lava whose surface and sparks move as the original's do, barred doors, the
save point) shaded in more colours. The title is a picture: the original's
title, subtitle and underline over a cavern.

Outputs (checked in):
  gfx/gfx_<k>.bin    bank GFX_BANK: tiles (16 x 16, column-major, 256
                     bytes each), then the sprite patterns (128 bytes each)
  gfx/title_<k>.bin  the title picture, 320 x 256 column-major (5 banks)
  src/gfx_data.asm   the BANK_n sections (with gfx/world.bin)
  src/gfx_data.c, .h palettes, tile and pattern numbers

    python3 tools/gen_gfx.py
"""
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "..", "..", "sdk", "tools"))
from nextpic import avoid_transparent, rgb9  # noqa: E402

ORIG = os.path.join(ROOT, "..", "..", "..", "amiga_mcp", "examples", "orb_hunter")
L2_BANK, WORLD_BANK, WORK_BANK, GFX_BANK, MUSIC_BANK, TITLE_BANK = 16, 21, 22, 23, 24, 25

# the original's palette (main.c), pens 0..15
PAL12 = [0x001, 0xFFF, 0x0A9, 0xFC0, 0x141, 0x262, 0x668, 0x99A,
         0xF30, 0xF80, 0xFF0, 0xA0F, 0xF0A, 0x06F, 0x0FF, 0x0F0]
(BG, WHITE, HUNTER, HUNTER2, ROCK, ROCK2, METAL, METAL2, LAVA, LAVA2, MISSILE, ENEMY, ENEMY2, DOOR, ITEM,
 BEAM) = range(16)


def rgb(c12):
    return ((c12 >> 8) * 17, ((c12 >> 4) & 15) * 17, (c12 & 15) * 17)


# ---- Layer 2 palette: the original's 16, then shades for the tiles
pal = [rgb(c) for c in PAL12]


def shade(c, f):
    return tuple(max(0, min(255, int(v * f))) for v in c)


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def ramp(a, b, n):
    """n pens from colour a to b; returns their indices"""
    first = len(pal)
    for i in range(n):
        pal.append(mix(a, b, i / max(1, n - 1)))
    return list(range(first, first + n))


ROCKS = ramp(shade(rgb(0x141), 0.55), shade(rgb(0x262), 1.25), 8)
METALS = ramp(shade(rgb(0x668), 0.45), shade(rgb(0x99A), 1.15), 8)
LAVAS = ramp(shade(rgb(0xF30), 0.55), rgb(0xFE6), 8)
DOORS = ramp(shade(rgb(0x06F), 0.4), mix(rgb(0x06F), (255, 255, 255), 0.5), 6)
TITLES = ramp(shade(rgb(0x0A9), 0.6), mix(rgb(0x0A9), (255, 255, 255), 0.6), 8)
CAVE_BG = ramp(rgb(0x001), mix(rgb(0x001), rgb(0x141), 0.35), 6)


def lcg(seed):
    while True:
        seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
        yield seed >> 16


# ---- tiles (16 x 16 pen indices), the original's draw_tile designs
def tile_rock(seed=1):
    t = np.zeros((16, 16), np.uint8)
    r = lcg(seed)
    for y in range(16):
        for x in range(16):
            t[y, x] = ROCKS[2 + (next(r) % 3) + (1 if y < 2 else 0) - (1 if y > 13 else 0)]
    for x, y in ((3, 2), (10, 5), (6, 11), (13, 9), (1, 7), (8, 14), (14, 2), (5, 6)):   # its specks
        t[y, x] = ROCKS[7]
        if x + 1 < 16:
            t[y, x + 1] = ROCKS[6]
    return t


def tile_brick():
    t = np.zeros((16, 16), np.uint8)
    r = lcg(7)
    for y in range(16):
        for x in range(16):
            t[y, x] = ROCKS[3 + next(r) % 2 + (1 if y % 4 == 0 else 0)]
    for y in (3, 7, 11, 15):
        t[y, :] = ROCKS[6]
    for x, y0 in ((7, 0), (15, 4), (7, 8), (15, 12)):
        t[y0:y0 + 4, x] = ROCKS[6]
    return t


def tile_metal():
    t = np.zeros((16, 16), np.uint8)
    for y in range(16):
        t[y, :] = METALS[5 - y // 6]
    t[0, :] = METALS[7]
    t[:, 0] = METALS[7]
    t[15, :] = BG
    t[:, 15] = BG
    t[14, 1:15] = METALS[1]
    t[1:15, 14] = METALS[1]
    for x, y in ((3, 3), (11, 3), (3, 11), (11, 11)):          # rivets
        t[y, x] = METALS[7]
        t[y + 1, x + 1] = METALS[0]
    return t


def tile_platform():
    t = np.zeros((16, 16), np.uint8)
    t[0, :] = ROCKS[6]
    t[1:3, :] = ROCKS[4]
    t[3, :] = ROCKS[7]
    return t


def tile_lava(frame):
    """draw_tile's lava at g_frame = frame (0..15): body from row 2, the wavy
    surface, three sparks moving across"""
    t = np.zeros((16, 16), np.uint8)
    for y in range(2, 16):
        t[y, :] = LAVAS[max(0, 3 - (y - 2) // 4)]
    wave = (frame // 4) & 3
    for x in range(16):
        wy = 0 if ((x + wave) & 3) > 1 else 1
        t[wy, x] = LAVAS[6]
        t[wy + 1, x] = LAVAS[5]
    for dx, y in ((3, 6), (9, 10), (5, 13)):
        x = (frame + dx) & 15
        t[y, x] = LAVAS[7]
        t[y, (x + 1) & 15] = LAVAS[5]
    return t


def tile_door():
    t = np.zeros((16, 16), np.uint8)
    for x0 in (2, 7, 12):
        t[:, x0:x0 + 3] = DOORS[3]
        t[:, x0] = DOORS[5]
        t[:, x0 + 2] = DOORS[1]
    for y0 in (3, 11):
        t[y0:y0 + 2, 2:15] = DOORS[4]
        t[y0 + 1, 2:15] = DOORS[2]
    return t


FONT = None


def font():
    global FONT
    if FONT is None:
        src = open(os.path.join(ORIG, "draw.c")).read()
        body = re.sub(r"/\*.*?\*/", "", src.split("font_5x7[96][7] = {")[1].split("};")[0], flags=re.S)
        FONT = [[int(v, 0) for v in r.split(",") if v.strip()] for r in re.findall(r"\{([^{}]*)\}", body)]
    return FONT


def draw_char(img, x, y, ch, pen, scale=1):
    g = font()[ord(ch) - 32] if 32 <= ord(ch) < 128 else font()[0]
    for r in range(7):
        for c in range(5):
            if g[r] & (0x10 >> c):
                img[y + r * scale:y + (r + 1) * scale, x + c * scale:x + (c + 1) * scale] = pen


def text_width(s, scale=1):
    return len(s) * 6 * scale


def draw_text(img, x, y, s, pen, scale=1):
    for i, ch in enumerate(s):
        draw_char(img, x + i * 6 * scale, y, ch, pen, scale)


def tile_save(flash):
    t = np.zeros((16, 16), np.uint8)
    t[2:14, 4:12] = ITEM if flash else BEAM
    draw_char(t, 5, 4, "S", WHITE)
    return t


TILES = [("ROCK", tile_rock()), ("BRICK", tile_brick()), ("METAL", tile_metal()), ("PLATFORM", tile_platform()),
         ("DOOR", tile_door()), ("SAVE0", tile_save(0)), ("SAVE1", tile_save(1))]
TILES += [(f"LAVA{f}", tile_lava(f)) for f in range(16)]


# ---- sprites: draw.c's routines on a canvas, pen 0 = transparent
class Canvas:
    def __init__(self, w=16, h=16):
        self.p = np.zeros((h, w), np.uint8)

    def rect(self, x0, y0, x1, y1, pen):
        self.p[y0:y1 + 1, x0:x1 + 1] = pen

    def plot(self, x, y, pen):
        self.p[y, x] = pen


def hunter_standing(facing):
    """draw_hunter_standing at x = 2 (its cannon reaches x - 1 facing left)"""
    c, x = Canvas(), 2
    c.rect(x + 2, 0, x + 9, 3, HUNTER)
    if facing == 0:
        c.rect(x + 7, 1, x + 9, 2, HUNTER2)
    else:
        c.rect(x + 2, 1, x + 4, 2, HUNTER2)
    c.rect(x + 1, 4, x + 10, 9, HUNTER)
    c.rect(x + 4, 5, x + 7, 6, HUNTER2)
    if facing == 0:
        c.rect(x + 9, 5, x + 12, 7, HUNTER)
        c.rect(x + 12, 5, x + 12, 7, HUNTER2)
    else:
        c.rect(x - 1, 5, x + 2, 7, HUNTER)
        c.rect(x - 1, 5, x - 1, 7, HUNTER2)
    c.rect(x + 2, 10, x + 4, 14, HUNTER)
    c.rect(x + 7, 10, x + 9, 14, HUNTER)
    c.rect(x + 1, 14, x + 5, 15, HUNTER2)
    c.rect(x + 6, 14, x + 10, 15, HUNTER2)
    return c.p


def hunter_rolled(phase):
    """draw_hunter_rolled at x = 2 (ball at x + 2), phase = (g_frame / 2) & 3"""
    c, cx = Canvas(), 4
    c.rect(cx + 1, 0, cx + 6, 0, HUNTER)
    c.rect(cx, 1, cx + 7, 6, HUNTER)
    c.rect(cx + 1, 7, cx + 6, 7, HUNTER)
    sp = [(3, 1, 4, 2), (5, 3, 6, 4), (3, 5, 4, 6), (1, 3, 2, 4)][phase]
    c.rect(cx + sp[0], sp[1], cx + sp[2], sp[3], HUNTER2)
    return c.p


def crawler(facing):
    c = Canvas()
    c.rect(1, 4, 10, 8, ENEMY)
    if facing == 0:
        c.rect(8, 2, 11, 6, ENEMY)
    else:
        c.rect(0, 2, 3, 6, ENEMY)
    for x in (2, 5, 8):
        c.rect(x, 9, x + 1, 9, ENEMY2)
    c.plot(10 if facing == 0 else 1, 3, WHITE)
    return c.p


def flyer(wing):
    c = Canvas()
    c.rect(3, 3, 6, 7, ENEMY)
    if wing:
        c.rect(0, 1, 3, 3, ENEMY)
        c.rect(6, 1, 9, 3, ENEMY)
    else:
        c.rect(0, 4, 3, 6, ENEMY)
        c.rect(6, 4, 9, 6, ENEMY)
    c.plot(4, 4, ENEMY2)
    c.plot(5, 4, ENEMY2)
    c.rect(4, 8, 5, 9, ENEMY)
    return c.p


def hopper():
    c = Canvas()
    c.rect(2, 2, 7, 7, ENEMY)
    c.rect(1, 0, 8, 3, ENEMY)
    c.plot(3, 1, WHITE)
    c.plot(6, 1, WHITE)
    c.rect(0, 8, 2, 11, ENEMY2)
    c.rect(7, 8, 9, 11, ENEMY2)
    c.rect(0, 11, 3, 11, ENEMY2)
    c.rect(6, 11, 9, 11, ENEMY2)
    return c.p


def turret(flash):
    c = Canvas()
    c.rect(1, 8, 10, 11, METAL)
    c.rect(3, 5, 8, 8, METAL)
    c.rect(2, 2, 9, 5, ENEMY)
    c.rect(4, 0, 7, 1, ENEMY2)
    if flash:
        c.plot(5, 0, LAVA)
        c.plot(6, 0, LAVA)
    return c.p


def boss_beast():
    c = Canvas(32, 32)
    c.rect(4, 8, 19, 27, ENEMY)
    c.rect(6, 2, 17, 9, ENEMY)
    c.rect(8, 4, 9, 5, WHITE)
    c.rect(14, 4, 15, 5, WHITE)
    c.plot(9, 5, BG)      # pupils: pen 0 is see-through on a sprite; the room behind is pen 0 too
    c.plot(15, 5, BG)
    for y in (14, 18, 22):
        c.rect(7, y, 16, y + 2, ENEMY2)
    c.rect(0, 10, 5, 14, ENEMY)
    c.rect(18, 10, 23, 14, ENEMY)
    c.rect(0, 14, 2, 17, ENEMY2)
    c.rect(21, 14, 23, 17, ENEMY2)
    c.rect(5, 28, 9, 31, ENEMY)
    c.rect(14, 28, 18, 31, ENEMY)
    c.rect(7, 0, 8, 2, ENEMY2)
    c.rect(11, 0, 12, 1, ENEMY2)
    c.rect(15, 0, 16, 2, ENEMY2)
    return c.p


def boss_drake(wing):
    c = Canvas(32, 32)
    c.rect(4, 8, 15, 19, ENEMY)
    c.rect(10, 2, 18, 9, ENEMY)
    c.rect(16, 5, 19, 8, ENEMY)
    c.rect(13, 4, 14, 5, LAVA)
    if wing:
        c.rect(0, 4, 5, 8, ENEMY2)
        c.rect(0, 2, 2, 4, ENEMY2)
    else:
        c.rect(0, 8, 5, 12, ENEMY2)
        c.rect(0, 12, 2, 14, ENEMY2)
    c.rect(2, 18, 6, 20, ENEMY)
    c.rect(0, 20, 4, 22, ENEMY)
    c.rect(0, 22, 2, 23, ENEMY2)
    c.rect(6, 20, 8, 23, ENEMY)
    c.rect(12, 20, 14, 23, ENEMY)
    c.rect(5, 23, 9, 23, ENEMY2)
    c.rect(11, 23, 15, 23, ENEMY2)
    return c.p


def orb(pulse):
    c = Canvas()
    body = ITEM if pulse else WHITE
    c.rect(2, 0, 5, 0, body)
    c.rect(1, 1, 6, 1, body)
    c.rect(0, 2, 7, 5, body)
    c.rect(1, 6, 6, 6, body)
    c.rect(2, 7, 5, 7, body)
    c.plot(2, 2, WHITE)
    c.plot(3, 2, WHITE)
    return c.p


def beam():
    c = Canvas()
    c.rect(0, 0, 3, 1, BEAM)
    return c.p


def missile(right):
    c = Canvas()
    c.rect(0, 0, 5, 2, MISSILE)
    c.plot(5 if right else 0, 1, WHITE)
    return c.p


def bomb():
    c = Canvas()
    c.rect(1, 0, 2, 0, HUNTER2)
    c.rect(0, 1, 3, 2, HUNTER2)
    c.rect(1, 3, 2, 3, HUNTER2)
    return c.p


def blast(r):
    """draw_bombs' flash at radius r (2..8), the sprite's (0, 0) at bx - r + 2"""
    c = Canvas()
    n = 2 * r - 1                               # bx - r + 2 .. bx + r
    c.rect(0, 0, n - 1, n - 1, WHITE)
    if r > 2:
        c.rect(1, 1, n - 2, n - 2, HUNTER2)
    return c.p


def blast_corner():
    """a quarter of a bigger flash (r 9..12): white top and left edge, yellow
    inside; four of these, mirrored, make the square"""
    c = Canvas()
    c.rect(0, 0, 15, 15, HUNTER2)
    c.rect(0, 0, 15, 0, WHITE)
    c.rect(0, 0, 0, 15, WHITE)
    return c.p


def dot(pen):
    c = Canvas()
    c.rect(0, 0, 2, 2, pen)
    return c.p


def spark():
    c = Canvas()
    c.plot(0, 0, 1)
    c.plot(1, 0, 1)
    return c.p


def quads(img):
    """a 32 x 32 image -> four 16 x 16 patterns, TL TR BL BR"""
    return [img[y:y + 16, x:x + 16] for y in (0, 16) for x in (0, 16)]


SPRITES = [("HUNTER_R", hunter_standing(0)), ("HUNTER_L", hunter_standing(1))]
SPRITES += [(f"ROLL{k}", hunter_rolled(k)) for k in range(4)]
SPRITES += [("CRAWLER_R", crawler(0)), ("CRAWLER_L", crawler(1)), ("FLYER0", flyer(0)), ("FLYER1", flyer(1)),
            ("HOPPER", hopper()), ("TURRET0", turret(0)), ("TURRET1", turret(1))]
SPRITES += [(f"BEAST{k}", q) for k, q in enumerate(quads(boss_beast()))]
SPRITES += [(f"DRAKE{w}_{k}", q) for w in range(2) for k, q in enumerate(quads(boss_drake(w)))]
SPRITES += [("ORB0", orb(0)), ("ORB1", orb(1)), ("BEAM", beam()), ("MISSILE_R", missile(1)),
            ("MISSILE_L", missile(0)), ("BOMB", bomb())]
SPRITES += [(f"BLAST{r}", blast(r)) for r in range(2, 9)]
SPRITES += [("BLAST_CORNER", blast_corner()), ("EBULLET", dot(ENEMY2)), ("SPARK", spark())]


def pattern4(p):
    """16 x 16 pens -> 128 bytes, two pixels a byte, the left in the high nibble"""
    p = np.asarray(p, np.uint8)
    return bytes(((p[:, 0::2] << 4) | p[:, 1::2]).flatten())


# ---- the title: the original's draw_title over a cavern
def title_picture():
    img = np.zeros((256, 320), np.uint8)
    for y in range(256):                         # a dark cavern glow towards the floor
        img[y, :] = CAVE_BG[min(5, max(0, (y - 120) // 22))]
    r = lcg(3)
    for x in range(0, 320, 16):                  # stalactites and the floor in the tiles
        h = 1 + next(r) % 3
        for k in range(h):
            img[k * 16:(k + 1) * 16, x:x + 16] = TILES[0][1] if k < h - 1 or x % 32 else TILES[1][1]
    for x in range(0, 320, 16):
        img[224:240, x:x + 16] = TILES[0][1]
        img[240:256, x:x + 16] = TILES[0][1]
    for x in (96, 112, 192, 208):
        img[224:240, x:x + 16] = TILES[7][1]     # a lava pool (the first lava frame)
    s = "ORB HUNTER"
    cx = (320 - text_width(s, 3)) // 2
    for i, ch in enumerate(s):                   # its title, shaded top to bottom
        tmp = np.zeros((21, 18), np.uint8)
        draw_char(tmp, 0, 0, ch, 1, 3)
        for y in range(21):
            for x in range(18):
                if tmp[y, x]:
                    img[40 + y + 2, cx + i * 18 + x + 2] = BG
        for y in range(21):
            for x in range(18):
                if tmp[y, x]:
                    img[40 + y, cx + i * 18 + x] = TITLES[7 - y * 7 // 20]
    img[67:69, cx:cx + text_width(s, 3)] = HUNTER2
    s = "THE LOST CAVERNS"
    draw_text(img, (320 - text_width(s)) // 2, 80, s, ITEM)
    # the hunter on a ledge, and an orb (the sprites' own pixels, doubled)
    for name, scale, (x, y) in (("HUNTER_R", 2, (16, 192)), ("CRAWLER_L", 2, (276, 204)), ("ORB1", 2, (150, 96))):
        p = dict(SPRITES)[name]
        for yy in range(16):
            for xx in range(16):
                if p[yy, xx]:
                    img[y + yy * scale:y + (yy + 1) * scale, x + xx * scale:x + (xx + 1) * scale] = p[yy, xx]
    return img


def column_major(img):
    return np.ascontiguousarray(np.asarray(img, np.uint8).T).tobytes()


def bank_asm(prefix, data, bank):
    asm = []
    for k in range(0, len(data), 16384):
        name = f"gfx/{prefix}_{k // 16384}.bin"
        open(os.path.join(ROOT, name), "wb").write(data[k:k + 16384])
        asm += [f"SECTION BANK_{bank + k // 16384}", f'BINARY "{name}"']
    return asm


def main():
    assert len(pal) <= 256
    l2pal = avoid_transparent([int(c[0]) * 7 // 255 << 6 | int(c[1]) * 7 // 255 << 3 | int(c[2]) * 7 // 255
                               for c in pal] + [0] * (256 - len(pal)))
    l2pal = [rgb9(PAL12[i]) if i < 16 else l2pal[i] for i in range(256)]
    tiles = b"".join(column_major(t) for _, t in TILES)
    pats = b"".join(pattern4(p) for _, p in SPRITES)
    assert len(SPRITES) <= 128 and len(tiles) + len(pats) <= 16384
    gfx = tiles + pats
    asm = ["; generated by tools/gen_gfx.py - do not edit: Orb Hunter's data, in banks by the .nex", "",
           f"SECTION BANK_{WORLD_BANK}", 'BINARY "gfx/world.bin"']
    asm += bank_asm("gfx", gfx + bytes(-len(gfx) % 16384), GFX_BANK)
    asm += bank_asm("title", column_major(title_picture()), TITLE_BANK)
    open(os.path.join(ROOT, "src", "gfx_data.asm"), "w").write("\n".join(asm) + "\n")

    # sprite palette: group 0 is the original's 16 pens; group k (1..15) has
    # pen 1 = the original's pen k (sparks take their colour this way)
    spal = [rgb9(c) for c in PAL12]
    for k in range(1, 16):
        spal += [0, rgb9(PAL12[k])] + [0] * 14

    def arr(name, typ, vals, per=16):
        rows = [", ".join(str(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
        return f"const {typ} {name}[{len(vals)}] = {{\n    " + ",\n    ".join(rows) + "\n};\n"

    open(os.path.join(ROOT, "src", "gfx_data.c"), "w").write(
        "/* generated by tools/gen_gfx.py - do not edit */\n#include \"gfx_data.h\"\n\n"
        + arr("l2_pal", "uint16_t", l2pal) + arr("spr_pal", "uint16_t", spal))
    h = ["/* generated by tools/gen_gfx.py - do not edit */", "#ifndef GFX_DATA_H", "#define GFX_DATA_H",
         "#include <stdint.h>", "",
         f"#define GFX_TILES_OFF 0          /* in bank GFX_BANK: tile n at n * 256, column-major */",
         f"#define GFX_PATS_OFF {len(tiles)}     /* then the sprite patterns, 128 bytes each */",
         f"#define GFX_NPATS {len(SPRITES)}", ""]
    h += [f"#define T_{n} {i}" for i, (n, _) in enumerate(TILES)]
    h += [""] + [f"#define P_{n} {i}" for i, (n, _) in enumerate(SPRITES)]
    h += ["", "extern const uint16_t l2_pal[256];", "extern const uint16_t spr_pal[256];", "", "#endif", ""]
    open(os.path.join(ROOT, "src", "gfx_data.h"), "w").write("\n".join(h))
    print(f"palette {len(pal)} pens, {len(TILES)} tiles, {len(SPRITES)} sprite patterns, gfx {len(gfx)} bytes")


if __name__ == "__main__":
    main()
