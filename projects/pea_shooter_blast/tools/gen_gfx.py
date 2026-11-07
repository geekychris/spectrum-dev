#!/usr/bin/env python3
"""Pea Shooter Blast's pictures for the Spectrum Next.

The original (draw.c) draws every tile and sprite with RectFill, Draw and
WritePixel in a 16-colour palette (main.c). Here the same shapes are
redrawn on the host with shading in the Next's 256 colours:

  gfx/tiles.bin      the 13 tile types, 16 x 16, column-major (a column of a
                     tile is 16 bytes: Layer 2's 320 mode is column-major too,
                     so a map column is drawn with 16 copies)
  gfx/spr.bin        4-bit sprite patterns (128 x 128 bytes)
  src/gfx_data.c/.h  the Layer 2 palette (entries 1-15 the original's colours,
                     then the shades), the sprite palettes, pattern numbers,
                     the copper gradients behind each level

    python3 tools/gen_gfx.py        (from projects/pea_shooter_blast)
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "..", "..", "sdk", "tools"))
import nextpic  # noqa: E402

# main.c's palette
ORIG = [0x112, 0xFFF, 0x0A0, 0x0E0, 0x0F0, 0x666, 0x842, 0xAAA,
        0xF00, 0xF80, 0xFF0, 0xF0F, 0x531, 0x444, 0xFA0, 0x8FF]
(BG, WHITE, GREEN, LGREEN, LIME, GREY, BROWN, LGREY, RED, ORANGE, YELLOW,
 MAGENTA, DBROWN, DGREY, YORANGE, CYAN) = range(16)


def c12(c):
    return tuple(((c >> s) & 15) * 17 for s in (8, 4, 0))


def mix(a, b, t):
    return tuple(int(round(x + (y - x) * t)) for x, y in zip(a, b))


def shade(c, k):
    """k < 0 darker, k > 0 lighter (towards white)"""
    return mix(c, (0, 0, 0), -k) if k < 0 else mix(c, (255, 255, 255), k)


class Pal:
    """Layer 2 palette: the original's 16 first, then colours as they come"""

    def __init__(self):
        self.rgb9 = [nextpic.rgb9_from_rgb(*c12(c)) for c in ORIG]

    def idx(self, rgb):
        c = nextpic.rgb9_from_rgb(*rgb)
        if c in self.rgb9[1:]:
            return self.rgb9.index(c, 1)
        self.rgb9.append(c)
        assert len(self.rgb9) <= 256
        return len(self.rgb9) - 1


# ---- tiles (draw.c draw_tile, shaded) ----

def noise(seed):
    rng = np.random.default_rng(seed)
    return rng.random((16, 16))


def tile_images():
    """type -> 16 x 16 array of RGB tuples or None (background)"""
    T = {}
    E = None
    img = lambda: [[E] * 16 for _ in range(16)]

    # GROUND: brown earth, a lighter crust on top, speckles (the original's)
    g = img(); n = noise(1); base = c12(ORIG[BROWN])
    for y in range(16):
        for x in range(16):
            g[y][x] = shade(base, -0.25 * y / 15 + (n[y][x] - 0.5) * 0.25)
    for x in range(16):
        g[0][x] = shade(base, 0.45)
        g[1][x] = shade(base, 0.2)
    for i in range(4):
        g[4 + (i & 1) * 6][2 + i * 4] = c12(ORIG[LGREY])
    T[1] = g
    # BRICK: orange bricks with dark mortar (the original's lines), each brick shaded
    b = img(); base = c12(ORIG[ORANGE]); n = noise(2)
    for y in range(16):
        for x in range(16):
            mortar = y == 7 or y == 15 or (y < 7 and x == 7) or (y > 7 and x in (3, 11))
            if mortar:
                b[y][x] = (40, 20, 10)
            else:
                top = y in (0, 8)
                left = x in (0, 8) if y < 7 else x in (4, 12)
                b[y][x] = shade(base, (0.3 if top else 0) + (0.15 if left else 0) - 0.3 * ((y % 8) / 7) + (n[y][x] - 0.5) * 0.15)
    T[2] = b
    # ROCK: grey with darker and lighter pebbles
    r = img(); base = c12(ORIG[GREY]); n = noise(3)
    for y in range(16):
        for x in range(16):
            r[y][x] = shade(base, (n[y][x] - 0.5) * 0.35)
    for (x, y) in ((3, 2), (8, 5), (12, 9), (5, 11)):
        r[y][x] = shade(base, -0.5); r[y][x + 1] = shade(base, -0.35)
    for (x, y) in ((6, 3), (10, 7), (2, 13)):
        r[y][x] = shade(base, 0.45); r[y + 1][x] = shade(base, 0.25)
    T[3] = r
    # PLATFORM: a plank (rows 0-3) with a shadowed lower edge (row 4)
    p = img(); base = c12(ORIG[BROWN])
    for x in range(16):
        p[0][x] = shade(base, 0.4)
        p[1][x] = shade(base, 0.15)
        p[2][x] = base
        p[3][x] = shade(base, -0.2)
        p[4][x] = c12(ORIG[GREY])
    for x in (5, 11):
        p[1][x] = p[2][x] = shade(base, -0.35)
    T[4] = p
    # LADDER: rails at x 3 and 12, rungs at y 4 and 10, lit on the left
    l = img(); base = c12(ORIG[YORANGE])
    for y in range(16):
        l[y][3] = shade(base, 0.25); l[y][12] = shade(base, -0.15)
        l[y][4] = shade(base, -0.4); l[y][13] = shade(base, -0.55)
    for y in (4, 10):
        for x in range(4, 12):
            l[y][x] = base
            l[y + 1][x] = shade(base, -0.5)
    T[5] = l
    # SPIKES: four red spikes, lit on one side, bright tips
    s = img(); base = c12(ORIG[RED])
    for i in range(4):
        bx = i * 4
        for y in range(16):
            half = 2 * y / 15                # 0 at the tip, 2 at the base
            for x in range(16):
                dx = x - (bx + 1.5)
                if -half - 0.5 <= dx <= half + 0.5 and bx <= x <= bx + 3:
                    s[y][x] = shade(base, 0.5 if y < 3 else (0.15 if dx < 0 else -0.35))
    T[6] = s
    # PIPE_H: a green pipe (rows 4-11) shaded as a cylinder
    ph = img(); base = c12(ORIG[LGREEN])
    prof = [0.5, 0.3, 0.1, 0, -0.1, -0.25, -0.4, -0.6]
    for k, y in enumerate(range(4, 12)):
        for x in range(16):
            ph[y][x] = shade(base, prof[k] - (0.2 if x in (0, 15) else 0))
    T[7] = ph
    pv = img()
    for k, x in enumerate(range(4, 12)):
        for y in range(16):
            pv[y][x] = shade(base, prof[k] - (0.2 if y in (0, 15) else 0))
    T[8] = pv
    # DIRT: dark brown with texture
    d = img(); base = c12(ORIG[DBROWN]); n = noise(9)
    for y in range(16):
        for x in range(16):
            d[y][x] = shade(base, (n[y][x] - 0.5) * 0.4)
    T[9] = d
    # METAL: light grey panel, dark top and left edges, rivets (the original's), a bevel
    m = img(); base = c12(ORIG[LGREY])
    for y in range(16):
        for x in range(16):
            m[y][x] = shade(base, -0.1 * (x + y) / 30)
    for i in range(16):
        m[0][i] = m[i][0] = c12(ORIG[GREY])
        m[15][i] = m[i][15] = shade(base, -0.3)
        m[1][i] = shade(base, 0.3) if 0 < i < 15 else m[1][i]
    for (x, y) in ((2, 2), (13, 2), (2, 13), (13, 13)):
        m[y][x] = c12(ORIG[WHITE]); m[y + 1][x + 1] = shade(base, -0.45)
    T[10] = m
    # GATE: a glowing cyan door (x 2-13) with white posts and a dark arrow
    gt = img(); base = c12(ORIG[CYAN])
    for y in range(16):
        for x in range(2, 14):
            gt[y][x] = shade(base, -0.3 * abs(x - 7.5) / 6 + 0.1 * ((y // 2) % 2))
        gt[y][2] = gt[y][13] = c12(ORIG[WHITE])
    for (x0, y0, x1, y1) in ((5, 4, 10, 7), (10, 7, 5, 11)):
        n_ = max(abs(x1 - x0), abs(y1 - y0))
        for k in range(n_ + 1):
            gt[round(y0 + (y1 - y0) * k / n_)][round(x0 + (x1 - x0) * k / n_)] = (0, 0, 40)
    T[11] = gt
    return T


# ---- sprites: 4-bit, pens of sprite palette 0 ----

SP = {  # pen: colour
    "white": (1, c12(0xFFF)), "body": (2, c12(0x0A0)), "body_d": (3, c12(0x070)), "turret": (4, c12(0x0E0)),
    "turret_l": (5, c12(0x8F8)), "tread": (6, c12(0x444)), "tread_l": (7, c12(0x777)), "red": (8, c12(0xF00)),
    "red_d": (9, c12(0x900)), "orange": (10, c12(0xF80)), "yellow": (11, c12(0xFF0)), "magenta": (12, c12(0xF0F)),
    "metal": (13, c12(0xAAA)), "lime": (14, c12(0x0F0)), "dark": (15, c12(0x112)),
}
P = {k: v[0] for k, v in SP.items()}


def blank(w=16, h=16):
    return np.zeros((h, w), np.uint8)


def rect(a, x0, y0, x1, y1, pen):
    """RectFill: inclusive corners, clipped"""
    h, w = a.shape
    a[max(y0, 0):min(y1, h - 1) + 1, max(x0, 0):min(x1, w - 1) + 1] = pen


def tank(tread_off):
    a = blank()
    rect(a, 1, 2, 14, 9, P["body"]); rect(a, 1, 8, 14, 9, P["body_d"]); rect(a, 1, 2, 14, 2, P["turret"])
    rect(a, 4, 0, 11, 4, P["turret"]); rect(a, 4, 0, 11, 0, P["turret_l"])
    rect(a, 11, 1, 15, 3, P["turret"]); rect(a, 11, 1, 15, 1, P["turret_l"])          # the barrel, facing right
    rect(a, 0, 10, 15, 13, P["tread"]); rect(a, 1, 10, 14, 10, P["tread_l"])
    for x in range(tread_off, 16, 4):
        rect(a, x, 10, x, 13, P["dark"])
    return a


def dots(n):
    a = blank()
    for d in range(n):
        a[1, 5 + d * 2] = P["yellow"]
    return a


def walker():
    a = blank()
    rect(a, 2, 2, 13, 11, P["red"]); rect(a, 2, 9, 13, 11, P["red_d"])
    rect(a, 4, 0, 11, 3, P["orange"])
    a[4, 5] = a[4, 10] = P["white"]
    rect(a, 3, 12, 5, 15, P["red"]); rect(a, 10, 12, 12, 15, P["red"])
    return a


def flyer(up):
    a = blank()
    rect(a, 4, 4, 11, 11, P["orange"])
    if up:
        rect(a, 0, 0, 3, 6, P["red"]); rect(a, 12, 0, 15, 6, P["red"])
    else:
        rect(a, 0, 6, 3, 12, P["red"]); rect(a, 12, 6, 15, 12, P["red"])
    a[6, 6] = a[6, 9] = P["white"]
    return a


def turret():
    a = blank()
    rect(a, 2, 8, 13, 15, P["metal"]); rect(a, 2, 8, 13, 8, P["white"])
    rect(a, 4, 4, 11, 8, P["red"])
    rect(a, 11, 5, 15, 7, P["red"])                     # the barrel, facing right
    return a


def hopper(jump):
    a = blank()
    rect(a, 3, 4, 12, 12, P["red"]); rect(a, 4, 2, 11, 5, P["orange"])
    a[3, 5] = a[3, 10] = P["white"]
    if jump:
        rect(a, 1, 13, 4, 15, P["red"]); rect(a, 11, 13, 14, 15, P["red"])
    else:
        rect(a, 2, 12, 5, 15, P["red"]); rect(a, 10, 12, 13, 15, P["red"])
    return a


def boss():
    """32 x 32 with the boss at (4, 4): draw.c's -4..19 square and face"""
    a = blank(32, 32)
    o = 4
    rect(a, o - 4, o - 4, o + 19, o + 19, P["magenta"])
    rect(a, o - 2, o - 2, o + 17, o + 17, P["red"]); rect(a, o - 2, o + 14, o + 17, o + 17, P["red_d"])
    rect(a, o + 2, o + 2, o + 5, o + 5, P["white"]); rect(a, o + 10, o + 2, o + 13, o + 5, P["white"])
    a[o + 4, o + 4] = a[o + 4, o + 12] = P["dark"]
    rect(a, o + 3, o + 9, o + 12, o + 12, P["magenta"])
    return [a[y:y + 16, x:x + 16] for y in (0, 16) for x in (0, 16)]


def bar(green):
    """8 pixels of the boss's health bar (2 rows): `green` of them green"""
    a = blank()
    rect(a, 0, 0, 7, 1, P["red"])
    if green:
        rect(a, 0, 0, green - 1, 1, P["lime"])
    return a


def bullet(size):
    a = blank()
    if size == 0:
        a[8, 8] = a[8, 9] = P["yellow"]
    elif size == 1:
        rect(a, 8, 7, 11, 9, P["yellow"])
    else:
        rect(a, 7, 7, 12, 10, P["yellow"]); rect(a, 8, 8, 10, 8, P["white"])
    return a


def ebullet():
    a = blank()
    rect(a, 7, 7, 9, 9, P["red"]); a[7, 7] = P["orange"]
    return a


def pixel():
    a = blank()
    a[8, 8] = 1                                          # pen 1 of the particle's palette
    return a


def square(side):
    """a side x side square centred at (8, 8) in pen 1 (white, or the yellow palette)"""
    a = blank()
    h = side // 2
    rect(a, 8 - h, 8 - h, 8 - h + side - 1, 8 - h + side - 1, 1)
    return a


FONT = None


def powerup(weapon):
    a = blank()
    if weapon:
        rect(a, 2, 2, 13, 13, P["yellow"]); rect(a, 2, 2, 13, 2, P["white"])
        g = FONT[ord("W") - 32]
        for r in range(7):
            for c in range(5):
                if g[r] & (0x10 >> c):
                    a[4 + r, 4 + c] = P["dark"]
    else:
        rect(a, 2, 2, 13, 13, P["lime"]); rect(a, 2, 2, 13, 2, P["white"])
        rect(a, 5, 7, 10, 7, P["white"]); rect(a, 7, 5, 7, 10, P["white"])
    return a


def to4(a):
    return bytes(int(a[y, x]) << 4 | int(a[y, x + 1]) for y in range(16) for x in range(0, 16, 2))


def parse_font():
    import re
    src = open(os.path.join(HERE, "orig", "draw.c")).read()
    body = src[src.index("font_5x7[96][7]"):]
    return [[int(v, 16) for v in m.split(",")] for m in re.findall(r"\{(0x[0-9A-F]{2}(?:,0x[0-9A-F]{2}){6})\}", body)]


def main():
    global FONT
    FONT = parse_font()
    assert len(FONT) == 96
    pal = Pal()
    T = tile_images()
    tiles = bytearray(13 * 256)
    for t in range(13):
        im = T.get(t)
        for x in range(16):
            for y in range(16):
                tiles[t * 256 + x * 16 + y] = 0 if im is None or im[y][x] is None else pal.idx(im[y][x])
    os.makedirs(os.path.join(ROOT, "gfx"), exist_ok=True)
    open(os.path.join(ROOT, "gfx", "tiles.bin"), "wb").write(bytes(tiles))

    pats, names = [], {}

    def add(name, a):
        names.setdefault(name, len(pats))
        pats.append(to4(a))

    add("TANK", tank(0)); add("TANK", tank(2))
    for n in range(5):
        add("DOTS", dots(n))
    add("WALKER", walker())
    add("FLYER", flyer(0)); add("FLYER", flyer(1))
    add("TURRET", turret())
    add("HOPPER", hopper(0)); add("HOPPER", hopper(1))
    for q in boss():
        add("BOSS", q)
    for g in range(9):
        add("BAR", bar(g))
    for s in range(3):
        add("BULLET", bullet(s))
    add("EBULLET", ebullet())
    add("PIXEL", pixel())
    for side in range(1, 17):
        add("SQUARE", square(side))
    add("POWERUP", powerup(0)); add("POWERUP", powerup(1))
    spr = b"".join(pats)
    open(os.path.join(ROOT, "gfx", "spr.bin"), "wb").write(spr + bytes(16384 - len(spr)))

    # sprite palettes: 0 the sprites' pens; 1-4 pen 1 = a particle colour
    # (red, orange, yellow, green: draw.c's colours 8, 9, 10, 4); 5 pen 1 white
    r9 = lambda rgb: nextpic.rgb9_from_rgb(*rgb)
    spal = [0] * 256
    for _, (pen, rgb) in SP.items():
        spal[pen] = r9(rgb)
    for k, c in enumerate((ORIG[RED], ORIG[ORANGE], ORIG[YELLOW], ORIG[LIME], ORIG[WHITE])):
        spal[16 * (k + 1) + 1] = r9(c12(c))

    # the copper gradients behind the levels (64 bands of 4 lines): hand-picked
    # 9-bit steps (computed blends of such dark colours round each channel at
    # a different place and come out red or olive), from near the original's
    # background (0x112) at the top
    r9_ = lambda r, g, b: r << 6 | g << 3 | b
    steps = ([r9_(0, 0, 1), r9_(0, 0, 2), r9_(1, 1, 2), r9_(1, 1, 3), r9_(2, 2, 3)],      # the tunnel: blue
             [r9_(0, 0, 0), r9_(1, 0, 0), r9_(1, 1, 0), r9_(2, 1, 0), r9_(2, 1, 1)],      # the caves: brown
             [r9_(0, 0, 1), r9_(0, 1, 1), r9_(0, 1, 2), r9_(1, 2, 2), r9_(1, 2, 3)])      # the factory: teal
    grads = []
    for st in steps:
        g = []
        for k in range(64):
            v = k / 63 * (len(st) - 1)
            i = int(v)
            frac = v - i
            pick = i + 1 if i + 1 < len(st) and frac >= 0.5 else i
            g.append(st[pick])
        grads.append(g)

    def arr(t, name, vals, per=16):
        rows = [", ".join(str(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
        return f"const {t} {name}[{len(vals)}] = {{\n    " + ",\n    ".join(rows) + "\n};\n"

    l2 = pal.rgb9 + [0] * (256 - len(pal.rgb9))
    h = ["/* generated by tools/gen_gfx.py - do not edit */", "#ifndef GFX_DATA_H", "#define GFX_DATA_H",
         "#include <stdint.h>"]
    for k, v in names.items():
        h.append(f"#define SPR_{k} {v}")
    h += [f"#define SPR_NPAT {len(pats)}",
          "#define SPAL_PART 1                  /* + 0..3: red, orange, yellow, green; + 4 white */",
          "extern const uint16_t l2_pal[256], spr_pal[256];",
          "extern const uint16_t level_grad[3][64];",
          "#endif", ""]
    open(os.path.join(ROOT, "src", "gfx_data.h"), "w").write("\n".join(h))
    c = ["/* generated by tools/gen_gfx.py - do not edit */", '#include "gfx_data.h"', "",
         arr("uint16_t", "l2_pal", l2), arr("uint16_t", "spr_pal", spal),
         "const uint16_t level_grad[3][64] = {\n" + ",\n".join(
             "    { " + ", ".join(str(v) for v in g) + " }" for g in grads) + "\n};\n"]
    open(os.path.join(ROOT, "src", "gfx_data.c"), "w").write("\n".join(c))
    print(f"palette {len(pal.rgb9)} colours, {len(pats)} sprite patterns")


if __name__ == "__main__":
    main()
