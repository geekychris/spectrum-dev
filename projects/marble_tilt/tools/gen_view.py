#!/usr/bin/env python3
"""Marble Tilt's board for the Next, from the original's render.c.

The original draws the board in 3D every frame: the floor in runs a row at a
time, the walls as blocks, the gates, the bumpers, all through the amiga3d
camera, tilted at twice the tilt. Here the same geometry and camera
(geom.py) are worked out on the host for each tilt the original eases
through (11 x 11 steps), and the Next only fills the polygons (l2poly):

  gfx/bank_N.bin   the vertex tables: for each tilt (roll step, pitch step),
                   2048 bytes: the grid's corners at the floor, wall and
                   open-gate heights and the slab's front edge (int16 x, y)
                   - four tilts an 8K page
                   then the bumpers' rings (8 round the top, 8 round the
                   bottom) of the levels that have them, 256 bytes a tilt
                   then each level's polygon list (in the original's draw
                   order, see polys()), in one page
  src/view_data.c  the palette, where the tables are
  build/preview_*.png  the host's picture of each level (also what the
                   test compares the Next's Layer 2 with: gfx/expected_*.png)

    python3 tools/gen_view.py
"""
import os
import struct
import sys

from geom import (BUMPER_H, BUMPER_R, GATE_OPEN_H, HALF, MT_CELL, MT_N, TILTS, View, WALL_H, a3_cos, a3_sin,
                  tilt_value)

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)

# port: the original's 320 x 256 view at 7/8 scale (fov 266 -> 233), centred
# so the whole board fits Layer 2's 256 x 192 at every tilt
FOV, CX, CY = 233, 128, 60
VIEW_PAGE = 52                       # first 8K page of the vertex tables
VIEW_PAGES = (2 * TILTS + 1) ** 2 // 4 + 1
BUMP_PAGE = VIEW_PAGE + VIEW_PAGES   # then the bumper tables, 4 pages a level that has bumpers

# the levels (the original's mt_core.c)
LEVELS = []
src = open(os.path.join(HERE, "orig", "mt_core.c")).read()
body = src[src.index("mt_levels[MT_LEVELS][MT_N] = {"):]
for blk in body.split("{ ")[1:9]:
    rows = [s for s in blk.split('"')[1::2]]
    LEVELS.append(rows[:MT_N])
assert len(LEVELS) == 8 and all(len(r) == MT_N and all(len(x) == MT_N for x in r) for r in LEVELS)

# the original's palette (render.c, Amiga 12-bit) -> Layer 2 indices:
#   0..15   the void, a band each 16 display lines (the original's copper
#           gradient 0x225 -> 0x001, plat_sky)
#   16..22  floor, floor (odd rows), sand, checkpoint, exit, start, the slab's edge
#   32 + row * 8 + k   blocks by board row (the marble sprite's mask reads
#           the row back): k 0 wall top, 1 wall side, 2 gate shut,
#           3 gate open / gate side, 4 bumper top, 5 bumper side
AMIGA = [0x000, 0xFFF, 0x123, 0x99A, 0x113, 0x9AB, 0x89A, 0x445, 0x6A8, 0x386, 0xCA6, 0x48E, 0x3D5,
         0xE44, 0x844, 0xF53, 0xA32, 0xDDE, 0xFFF, 0x334, 0xCC8]
C_FLOOR, C_SAND, C_CP, C_EXIT, C_START, C_EDGE = 16, 18, 19, 20, 21, 22
K_WTOP, K_WSIDE, K_GSHUT, K_GOPEN, K_BTOP, K_BSIDE = range(6)
BLOCK_PENS = [8, 9, 13, 14, 15, 16]


def mix(a, b, k, n):
    """plat_sky's mix of two 12-bit colours"""
    out = 0
    for sh in (8, 4, 0):
        ca, cb = (a >> sh) & 15, (b >> sh) & 15
        out |= (ca + (cb - ca) * k // n) << sh
    return out


def palette():
    pal = [0x000] * 256
    for k in range(16):
        pal[k] = mix(0x225, 0x001, k, 15)
    for i, pen in enumerate([5, 6, 10, 11, 12, 20, 7]):
        pal[16 + i] = AMIGA[pen]
    for row in range(MT_N):
        for k, pen in enumerate(BLOCK_PENS):
            pal[32 + row * 8 + k] = AMIGA[pen]
    return pal


def rgb9(c12):
    """12-bit Amiga colour -> the Next's RGB333 (rounded)"""
    r, g, b = (c12 >> 8) & 15, (c12 >> 4) & 15, c12 & 15
    return (min(7, (r * 7 + 7) // 15) << 6) | (min(7, (g * 7 + 7) // 15) << 3) | min(7, (b * 7 + 7) // 15)


def rgb24(c12):
    return tuple(((c12 >> s) & 15) * 17 for s in (8, 4, 0))


# ---- vertices ----
H_INDEX = {0: 0, WALL_H: 1, GATE_OPEN_H: 2}
EDGE = 3 * 169                       # the slab's front edge, 2 points
BUMP = 512                           # bumper b, point k: 512 + b * 16 + k (0..7 top, 8..15 bottom)


def gv(h, r, c):
    """the grid corner at row line r (0 = far edge), column line c, height h"""
    return H_INDEX[h] * 169 + r * 13 + c


def grid_points():
    pts = [None] * 512
    for h, hi in H_INDEX.items():
        for r in range(13):
            for c in range(13):
                pts[hi * 169 + r * 13 + c] = (-HALF + c * MT_CELL, h, HALF - r * MT_CELL)
    pts[EDGE] = (-HALF, -16, -HALF)
    pts[EDGE + 1] = (HALF, -16, -HALF)
    return pts


def bumpers(level):
    return [(r, c) for r in range(MT_N) for c in range(MT_N) if LEVELS[level][r][c] == "B"]


def bumper_points(r, c):
    """render.c: the octagon round a bumper, top (y 18) then bottom (y 0)"""
    cx, cz = -HALF + c * MT_CELL + MT_CELL // 2, HALF - r * MT_CELL - MT_CELL // 2
    ring = []
    for k in range(8):
        a = k * 8192 + 4096
        ring.append((cx + ((a3_sin(a) * BUMPER_R) >> 16), cz + ((a3_cos(a) * BUMPER_R) >> 16)))
    return [(x, BUMPER_H, z) for x, z in ring] + [(x, 0, z) for x, z in ring]


# ---- the polygon lists (the original's board() and blocks()) ----
def floor_colour(ch, row):
    return {"Z": C_SAND, "C": C_CP, "E": C_EXIT, "S": C_START}.get(ch, None if ch in " #" else C_FLOOR + (row & 1))


def polys(level):
    """[(kind, colour, [vertex...])], kind 0 always, 1 gates open, 2 gates shut.
    Order (painter's): the slab's edge, the floor in runs a row at a time,
    then row by row from the far edge the blocks, gates and bumpers, each
    row's from the outside in (the original sorts by depth; this order
    gives the same picture for this camera)"""
    cells = LEVELS[level]
    out = [(0, C_EDGE, [gv(0, 12, 0), gv(0, 12, 12), EDGE + 1, EDGE])]
    for r in range(MT_N):
        c = 0
        while c < MT_N:
            col = floor_colour(cells[r][c], r)
            if col is None:
                c += 1
                continue
            e = c + 1
            while e < MT_N and floor_colour(cells[r][e], r) == col:
                e += 1
            out.append((0, col, [gv(0, r, c), gv(0, r, e), gv(0, r + 1, e), gv(0, r + 1, c)]))
            c = e
    bl = bumpers(level)
    for r in range(MT_N):
        items = []                   # (|x| of the inner side, the polygons)
        c = 0
        while c < MT_N:
            ch = cells[r][c]
            x0, x1 = -HALF + c * MT_CELL, -HALF + (c + 1) * MT_CELL
            if ch == "G":
                ps = []
                for kind, h, top in ((1, GATE_OPEN_H, K_GOPEN), (2, WALL_H, K_GSHUT)):
                    ps += block(r, c, c + 1, h, top, K_GOPEN, True, kind)
                items.append((min(abs(x0), abs(x1)), ps))
                c += 1
            elif ch == "B":
                items.append((min(abs(x0), abs(x1)), bumper(r, c, bl.index((r, c)))))
                c += 1
            elif ch == "#":
                e = c + 1
                while e < MT_N and cells[r][e] == "#":
                    e += 1
                near = r == MT_N - 1 or any(cells[r + 1][k] != "#" for k in range(c, e))
                xe = -HALF + e * MT_CELL
                items.append((0 if x0 < 0 < xe else min(abs(x0), abs(xe)), block(r, c, e, WALL_H, K_WTOP, K_WSIDE, near, 0)))
                c = e
            else:
                c += 1
        for _, ps in sorted(items, key=lambda t: -t[0]):
            out += ps
    return out


def block(r, c0, c1, h, top, side, near, kind):
    """render.c block(): the near side, the end facing the middle, the top"""
    base = 32 + r * 8
    x0, x1 = -HALF + c0 * MT_CELL, -HALF + c1 * MT_CELL
    ps = []
    if near:
        ps.append((kind, base + side, [gv(0, r + 1, c0), gv(0, r + 1, c1), gv(h, r + 1, c1), gv(h, r + 1, c0)]))
    if x1 < 0:
        ps.append((kind, base + side, [gv(0, r + 1, c1), gv(0, r, c1), gv(h, r, c1), gv(h, r + 1, c1)]))
    if x0 > 0:
        ps.append((kind, base + side, [gv(0, r + 1, c0), gv(0, r, c0), gv(h, r, c0), gv(h, r + 1, c0)]))
    ps.append((kind, base + top, [gv(h, r, c0), gv(h, r, c1), gv(h, r + 1, c1), gv(h, r + 1, c0)]))
    return ps


def bumper(r, c, b):
    """render.c: the sides that face the camera (the original's top covers
    the others), far ones first, then the top"""
    base = 32 + r * 8
    pts = bumper_points(r, c)
    v = View(0, 0)
    sides = []
    for k in range(8):
        j = (k + 1) & 7
        mid_z = (v.cam(*pts[8 + k])[2] + v.cam(*pts[8 + j])[2]) / 2
        centre_z = v.cam(-HALF + c * MT_CELL + 20, 0, HALF - r * MT_CELL - 20)[2]
        if mid_z < centre_z + 2:
            sides.append((-mid_z, [BUMP + b * 16 + 8 + k, BUMP + b * 16 + 8 + j, BUMP + b * 16 + j, BUMP + b * 16 + k]))
    out = [(0, base + K_BSIDE, vs) for _, vs in sorted(sides)]
    out.append((0, base + K_BTOP, [BUMP + b * 16 + k for k in range(8)]))
    return out


def view_of(ri, pi):
    return View(tilt_value(ri), tilt_value(pi), FOV, CX, CY)


def tilts():
    return [(ri, pi) for ri in range(-TILTS, TILTS + 1) for pi in range(-TILTS, TILTS + 1)]


def project_all(level, ri, pi):
    v = view_of(ri, pi)
    pts = {i: v.project(*p) for i, p in enumerate(grid_points()) if p}
    for b, (r, c) in enumerate(bumpers(level)):
        for k, p in enumerate(bumper_points(r, c)):
            pts[BUMP + b * 16 + k] = v.project(*p)
    return pts


def render(level, ri, pi, gates_open=True):
    """the host's picture of what the Next draws (PIL; l2poly's fill rule is
    close but not identical at the edges)"""
    from PIL import Image, ImageDraw
    pal = palette()
    img = Image.new("RGB", (256, 192))
    d = ImageDraw.Draw(img)
    for y in range(192):
        d.line([(0, y), (255, y)], fill=rgb24(pal[(y + 32) // 16]))
    pts = project_all(level, ri, pi)
    for kind, col, vs in polys(level):
        if kind == (2 if gates_open else 1):
            continue
        d.polygon([pts[i] for i in vs], fill=rgb24(pal[col]))
    return img


# ---- the marble (sprite) ----
MARBLE_SIZES = 10                    # radius 0..4.5 pixels in half pixels
SHADES = [0x445, 0x667, 0x889, 0xAAB, 0xCCD, 0xDDE, 0xEEF, 0xFFF]   # the original's 0xDDE body, 0xFFF shine


def marble_image(r2):
    """a shaded ball of radius r2 / 2 pixels round (0, 0): [(dx, dy, shade)];
    port: the original's flat octagon and shine, lit from the top left"""
    import math
    r = max(r2 / 2.0, 0.6)
    out = []
    for dy in range(-6, 7):
        for dx in range(-6, 7):
            d = math.hypot(dx, dy)
            if d > r + 0.25:
                continue
            nz = math.sqrt(max(0.0, 1 - min(1.0, (d / (r + 0.25)) ** 2)))
            light = max(0.0, (-dx * 0.5 - dy * 0.55) / (r + 0.25) + nz * 0.75)
            out.append((dx, dy, max(0, min(len(SHADES) - 1, int(light * len(SHADES))))))
    return out


def shadow_image():
    """render.c's shadow: the octagon of radius 9 on the floor, as pixels
    round its middle (at the board's middle, level)"""
    from PIL import Image, ImageDraw
    v = View(0, 0, FOV, CX, CY)
    oct8 = [(9, 0), (6, 6), (0, 9), (-6, 6), (-9, 0), (-6, -6), (0, -9), (6, -6)]
    cx, cy = v.project(0, 1, 0)
    pts = [v.project(x, 1, z) for x, z in oct8]
    img = Image.new("L", (32, 32))
    ImageDraw.Draw(img).polygon([(x - cx + 16, y - cy + 16) for x, y in pts], fill=1)
    return [(x - 16, y - 16) for y in range(32) for x in range(32) if img.getpixel((x, y))]


def write_marble():
    from geom import a3_cos as c, a3_sin as s
    v = View(0, 0, FOV, CX, CY)
    out = ['/* made by tools/gen_view.py: the marble\'s pictures and the shadow (see there) */',
           '#include "marble_data.h"', '']
    for i in range(MARBLE_SIZES):
        px = marble_image(i)
        out.append(f"static const int8_t img{i}[] = {{ " + ", ".join(f"{a}, {b}, {k}" for a, b, k in px) + ", 127 };")
    out.append("const int8_t *const marble_images[MARBLE_SIZES] = { " + ", ".join(f"img{i}" for i in range(MARBLE_SIZES)) + " };")
    sh = shadow_image()
    out.append("const int8_t marble_shadow[] = { " + ", ".join(f"{a}, {b}" for a, b in sh) + ", 127 };")
    out.append("const uint16_t marble_shades[MARBLE_SHADES] = { " + ", ".join(f"0x{rgb9(c12):03X}" for c12 in SHADES) + " };")
    with open(os.path.join(PROJ, "src", "marble_data.c"), "w") as f:
        f.write("\n".join(out) + "\n")
    with open(os.path.join(PROJ, "src", "marble_data.h"), "w") as f:
        f.write(f'''/* made by tools/gen_view.py: the marble sprite's pictures and the camera */
#ifndef MARBLE_DATA_H
#define MARBLE_DATA_H
#include <stdint.h>
#define MARBLE_SIZES  {MARBLE_SIZES}             /* radius in half pixels */
#define MARBLE_SHADES {len(SHADES)}
#define MARBLE_PAL    240           /* sprite palette entries of the shades */
#define MARBLE_FOV    {FOV}
#define MARBLE_CX     {CX}
#define MARBLE_CY     {CY}
#define MARBLE_CCP    {c(View.CAM_PITCH)}L        /* the camera's pitch: cos, sin (16.16) */
#define MARBLE_CSP    {s(View.CAM_PITCH)}L
extern const int8_t *const marble_images[MARBLE_SIZES];   /* dx, dy, shade ... 127 */
extern const int8_t marble_shadow[];                      /* dx, dy ... 127 */
extern const uint16_t marble_shades[MARBLE_SHADES];
#endif
''')


# ---- sprite patterns (bank SPR_BANK): 0 the marble (made at run time),
# 1..9 the tilt gauge (48 x 48, render.c hud()), 10 its dot, then the
# original's 16 x 16 font's glyphs for GLYPHS (the title, the clock)
SPR_BANK = 46
GLYPHS = "MARBLETIHOUGNP0123456789!:"
INK, GAUGE_RING, GAUGE_CROSS, GAUGE_DOT = 138, 139, 140, 141     # sprite palette entries


def font16():
    import re
    src = open(os.path.join(HERE, "orig", "amiga3d", "a3d_font16.h")).read()
    rows = re.findall(r"\{ (0x[0-9A-F]{4}(?:, 0x[0-9A-F]{4}){15}) \}", src)
    return [[int(v, 16) for v in r.split(", ")] for r in rows]


def sprite_bank():
    import math
    T = 0xE3
    pats = [bytearray([T] * 256) for _ in range(64)]
    big = [[T] * 48 for _ in range(48)]
    for y in range(48):
        for x in range(48):
            d = math.hypot(x - 23.5, y - 23.5)
            if abs(d - 18) < 0.8:
                big[y][x] = GAUGE_RING
            elif d < 18 and (abs(x - 23.5) < 0.6 or abs(y - 23.5) < 0.6):
                big[y][x] = GAUGE_CROSS
    for k in range(9):
        ox, oy = (k % 3) * 16, (k // 3) * 16
        for y in range(16):
            for x in range(16):
                pats[1 + k][y * 16 + x] = big[oy + y][ox + x]
    for y in range(16):
        for x in range(16):
            if math.hypot(x - 7.5, y - 7.5) < 3.2:
                pats[10][y * 16 + x] = GAUGE_DOT
    f = font16()
    for i, ch in enumerate(GLYPHS):
        g = f[ord(ch) - 32]
        for y in range(16):
            for x in range(16):
                if g[y] & (0x8000 >> x):
                    pats[11 + i][y * 16 + x] = INK
    with open(os.path.join(PROJ, "gfx", f"bank_{SPR_BANK}.bin"), "wb") as fo:
        fo.write(b"".join(bytes(p) for p in pats))


def main():
    write_marble()
    sprite_bank()
    pal = palette()
    gfx = os.path.join(PROJ, "gfx")
    os.makedirs(gfx, exist_ok=True)
    os.makedirs(os.path.join(PROJ, "build"), exist_ok=True)
    # vertex tables: 4 tilts a page, tilt t = (roll + 5) * 11 + (pitch + 5)
    data = bytearray()
    pts = grid_points()
    lo, hi = [999, 999], [-999, -999]
    for ri, pi in tilts():
        v = view_of(ri, pi)
        rec = bytearray(2048)
        for i, p in enumerate(pts):
            if p:
                x, y = v.project(*p)
                lo, hi = [min(lo[0], x), min(lo[1], y)], [max(hi[0], x), max(hi[1], y)]
                struct.pack_into("<hh", rec, i * 4, x, y)
        data += rec
    pages = [data[i:i + 8192] for i in range(0, len(data), 8192)]
    assert len(pages) == VIEW_PAGES
    # bumpers: 256 bytes a tilt (8 bumpers x 16 points x int16 x, y would be 512: they are uint8 x, int8 y - 64)
    bump_pages = {}
    page = BUMP_PAGE
    for lv in range(8):
        bl = bumpers(lv)
        if not bl:
            continue
        assert len(bl) <= 8
        data = bytearray()
        for ri, pi in tilts():
            v = view_of(ri, pi)
            rec = bytearray(256)
            for b, (r, c) in enumerate(bl):
                for k, p in enumerate(bumper_points(r, c)):
                    x, y = v.project(*p)
                    assert 0 <= x < 256 and 0 <= y < 192, (x, y)
                    rec[(b * 16 + k) * 2] = x
                    rec[(b * 16 + k) * 2 + 1] = y
            data += rec
        bump_pages[lv] = page
        for i in range(0, len(data), 8192):
            pages.append(data[i:i + 8192])
            page += 1
    # the polygon lists, one page after the tables: byte n | kind << 4, byte
    # colour, then n vertex offsets (uint16: grid vertex i -> i * 4, bumper
    # point -> 2048 + point * 2); 0 ends a list
    list_page = VIEW_PAGE + len(pages)
    blob, starts, sizes = bytearray(), [], []
    for lv in range(8):
        starts.append(0x4000 + len(blob))
        n0 = len(blob)
        for kind, col, vs in polys(lv):
            blob += bytes([len(vs) | kind << 4, col])
            for i in vs:
                blob += struct.pack("<H", i * 4 if i < 512 else 2048 + (i - BUMP) * 2)
        blob += b"\0"
        sizes.append(len(blob) - n0)
    assert len(blob) <= 8192
    pages.append(blob)
    if len(pages) & 1:
        pages.append(b"")
    for f in os.listdir(gfx):
        if f.startswith(("page_", "bank_")) and f != f"bank_{SPR_BANK}.bin":
            os.remove(os.path.join(gfx, f))
    asm = ["; made by tools/gen_view.py: the board's tables, straight into banks by the .nex", ""]
    for i in range(0, len(pages), 2):
        bank = (VIEW_PAGE + i) // 2
        with open(os.path.join(gfx, f"bank_{bank}.bin"), "wb") as f:
            f.write(bytes(pages[i]).ljust(8192, b"\0") + bytes(pages[i + 1]).ljust(8192, b"\0"))
        asm += [f"SECTION BANK_{bank}", f'BINARY "gfx/bank_{bank}.bin"']
    asm += [f"SECTION BANK_{SPR_BANK}", f'BINARY "gfx/bank_{SPR_BANK}.bin"']
    with open(os.path.join(PROJ, "src", "view_pages.asm"), "w") as f:
        f.write("\n".join(asm) + "\n")
    out = ['/* made by tools/gen_view.py from the original\'s render.c and levels: do not edit */',
           '#include "view_data.h"', '']
    out.append("const uint16_t view_lists[8] = { " + ", ".join(f"0x{a:04X}" for a in starts) + " };")
    out.append("const uint8_t view_bump_page[8] = { " + ", ".join(str(bump_pages.get(lv, 0)) for lv in range(8)) + " };")
    out.append("const uint16_t view_palette[128] = {")
    for i in range(0, 128, 16):
        out.append("    " + ", ".join(f"0x{rgb9(c):03X}" for c in pal[i:i + 16]) + ",")
    out.append("};")
    with open(os.path.join(PROJ, "src", "view_data.c"), "w") as f:
        f.write("\n".join(out) + "\n")
    with open(os.path.join(PROJ, "src", "view_data.h"), "w") as f:
        f.write(f'''/* made by tools/gen_view.py: the board's tables (see there) */
#ifndef VIEW_DATA_H
#define VIEW_DATA_H
#include <stdint.h>
#define VIEW_PAGE   {VIEW_PAGE}         /* vertex tables: 4 tilts a page, 2048 bytes each */
#define VIEW_TILTS  {TILTS}           /* steps each way */
#define VIEW_LIST_PAGE {list_page}     /* the polygon lists, read at 0x4000 */
#define VIEW_LAST_BANK {(VIEW_PAGE + len(pages)) // 2 - 1}
#define SPR_BANK    {SPR_BANK}          /* sprite patterns: 0 marble, 1-9 gauge, 10 its dot, 11.. glyphs */
#define SPR_GLYPHS  "{GLYPHS}"
#define SPR_INK     {INK}         /* sprite palette: glyph ink (+16 a palette offset), the gauge */
#define SPR_RING    {GAUGE_RING}
#define SPR_CROSS   {GAUGE_CROSS}
#define SPR_DOT     {GAUGE_DOT}
extern const uint16_t view_lists[8];       /* each level's list in VIEW_LIST_PAGE (address in slot 2) */
extern const uint8_t view_bump_page[8];    /* 0: no bumpers; 256 bytes a tilt, 32 a page */
extern const uint16_t view_palette[128];   /* RGB333, Layer 2 0..127 */
#endif
''')
    for lv in range(8):
        render(lv, 0, 0).save(os.path.join(PROJ, "build", f"preview_{lv + 1}.png"))
    render(3, 5, -5, False).save(os.path.join(PROJ, "build", "preview_4_tilt.png"))
    print(f"{len(pages)} pages from {VIEW_PAGE}; screen x {lo[0]}..{hi[0]} y {lo[1]}..{hi[1]}; "
          f"lists {sizes} bytes (page {list_page}); bumper pages {bump_pages}")


if __name__ == "__main__":
    sys.exit(main())
