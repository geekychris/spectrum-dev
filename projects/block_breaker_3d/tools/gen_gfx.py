#!/usr/bin/env python3
"""Block Breaker 3D's pictures for the Spectrum Next, from the original's
render.c (amiga_games block_breaker_3d): its camera, palette and geometry.

  gfx/tunnel_<k>.bin   the tunnel (320 x 256 Layer 2, banks 20-24): the
                       original's striped floor, ceiling and walls and the back
                       wall, seen from its camera (0, 0, -320), focal length
                       270, centre (160, 128). port: the camera doesn't lean
                       with the paddle (the picture is drawn once).
                       The walls are cut into depth bands, each with its own
                       palette entries (floor, ceiling, side), so the ring that
                       shows the ball's depth is a palette change: the band the
                       ball is in turns green (render.c's ring quads).
  gfx/spr.bin          4-bit sprite patterns (128 x 128 bytes): the ball (an
                       octagon with its highlight, as render.c draws it) at
                       each size, its shadow, the capsules and their letters,
                       the bits of broken bricks, the paddle's frame (normal
                       and wide) cut into 16 x 16 tiles
  src/gfx_data.c/.h    palettes, the bands, the screen position of every brick
                       corner (render.c's projection, done here), the size
                       classes by depth and the paddle's tiles

    python3 tools/gen_gfx.py        (from projects/block_breaker_3d)
"""
import bisect
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "..", "..", "sdk", "tools"))
import nextpic  # noqa: E402

# ---- the original's numbers (bb.h, render.c) ----
BB_COLS, BB_ROWS, BB_LAYERS = 6, 4, 3
BB_W, BB_H, BB_D = 168, 120, 800
BB_CW, BB_CH, BB_CD = 2 * BB_W // BB_COLS, 2 * BB_H // BB_ROWS, 60
BB_Z0 = BB_D - BB_LAYERS * BB_CD
BB_R = 10
BB_PAD_W, BB_PAD_H, BB_WIDE_W, BB_WIDE_H = 40, 28, 62, 42
CAM_Z, FOV, CX, CY = -320, 270, 160, 128
WALL_Z0, SEG = -200, 100
PALETTE = [0x000, 0xFFF, 0x102, 0x99A, 0x214, 0x325, 0x436, 0x213, 0x324, 0x224, 0x335,
           0xF33, 0xA12, 0xF83, 0xB52, 0xFE4, 0xBA2, 0x5E4, 0x2A3, 0x3DF, 0x19A, 0x56F, 0x23B,
           0xCCD, 0x889, 0x99A, 0x556, 0xFFE, 0x4FD, 0xF4E, 0x6F6, 0xFD3]
PEN_BACK, PEN_FLOOR, PEN_CEIL, PEN_SIDE = 4, 5, 7, 9
PEN_BALL, PEN_PADDLE, PEN_CAP, PEN_RING, PEN_YELLOW = 27, 28, 29, 30, 31
PEN_RED, PEN_GREEN, PEN_CYAN = 11, 17, 19

L2_BANK = 20
FIRST_BAND_PEN = 32
ZMIN, ZMAX = -64, 800            # the depths the moving things are drawn at


def tdiv(a, b):
    """C's integer division (towards 0)"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def proj_x(x, z):
    return CX + tdiv(x * FOV, z - CAM_Z)


def proj_y(y, z):
    return CY - tdiv(y * FOV, z - CAM_Z)


# ---- the bands ----

def bands():
    """band edges: equal steps on the screen between ZMIN and ZMAX, split at
    the stripes' ends, plus the stripes before ZMIN; even depths"""
    n = 60
    u0, u1 = 1.0 / (ZMIN - CAM_Z), 1.0 / (ZMAX - CAM_Z)
    e = {int(round((1.0 / (u0 + (u1 - u0) * k / n) + CAM_Z) / 2)) * 2 for k in range(n + 1)}
    e |= set(range(WALL_Z0, BB_D + 1, SEG)) | {ZMIN, ZMAX}
    return sorted(e)


EDGES = bands()
NB = len(EDGES) - 1
assert FIRST_BAND_PEN + 3 * NB <= 256, NB


def band_of(z):
    return max(0, min(NB - 1, bisect.bisect_right(EDGES, z) - 1))


def stripe(z):
    return ((z - WALL_Z0) // SEG) & 1


# ---- the tunnel picture ----

def tunnel():
    xs = (np.arange(320) + 0.5 - CX) / FOV
    ys = (CY - (np.arange(256) + 0.5)) / FOV
    dx, dy = np.meshgrid(xs, ys)                       # [y, x]
    with np.errstate(divide="ignore"):
        tw = np.where(dx != 0, BB_W / np.abs(dx), np.inf)
        tf = np.where(dy != 0, BB_H / np.abs(dy), np.inf)
    t = np.minimum(tw, tf)
    z = CAM_Z + t
    side = tw <= tf
    img = np.zeros((256, 320), np.uint8)
    for yy in range(256):
        for xx in range(320):
            zz = z[yy, xx]
            if zz >= BB_D:
                img[yy, xx] = PEN_BACK
                continue
            if zz < WALL_Z0:
                img[yy, xx] = 0
                continue
            b = band_of(int(np.floor(zz)))
            surf = 2 if side[yy, xx] else (0 if dy[yy, xx] < 0 else 1)
            img[yy, xx] = FIRST_BAND_PEN + 3 * b + surf
    return img


def rgb9r(c12):
    """12-bit Amiga RGB -> 9-bit, rounded (nextpic.rgb9 truncates)"""
    return sum(int(round(((c12 >> (8 - 4 * k)) & 15) * 7 / 15)) << (6 - 3 * k) for k in range(3))


def tunnel_colours():
    """the six stripe colours: 9-bit RGB loses the differences between the
    original's dark blues and purples (rounding makes the side walls' two
    stripes, and the floor's and the walls', the same colour), so these are
    picked by hand: each as close in hue as 3 bits allow, a surface's two
    stripes apart, and each surface apart from the walls at the same stripe"""
    r9 = lambda r, g, b: r << 6 | g << 3 | b
    return {PEN_FLOOR: r9(1, 0, 2), PEN_FLOOR + 1: r9(2, 1, 3),     # 0x325 0x436
            PEN_CEIL: r9(1, 0, 1), PEN_CEIL + 1: r9(1, 1, 2),       # 0x213 0x324
            PEN_SIDE: r9(1, 1, 2), PEN_SIDE + 1: r9(1, 1, 3)}       # 0x224 0x335


def base_palette():
    pal = [rgb9r(c) for c in PALETTE]
    for pen, c in tunnel_colours().items():
        pal[pen] = c
    return pal


def l2_palette():
    base = base_palette()
    pal = base + [0] * (256 - 32)
    for b in range(NB):
        s = stripe(EDGES[b])
        for surf, pen in enumerate((PEN_FLOOR, PEN_CEIL, PEN_SIDE)):
            pal[FIRST_BAND_PEN + 3 * b + surf] = base[pen + s]
    return nextpic.avoid_transparent(pal)


# ---- the bricks' corners on the screen ----
BZ = [BB_Z0 + 60 * l + d for l in range(3) for d in (-1, 0)] + [BB_D]       # 619 620 679 680 739 740 800
BX = [-BB_W + BB_CW * c for c in range(7)] + [-BB_W + BB_CW * c + 5 for c in range(6)] + \
     [-BB_W + BB_CW * (c + 1) - 5 for c in range(6)]
BY = [-BB_H + BB_CH * r for r in range(5)] + [-BB_H + BB_CH * r + 5 for r in range(4)] + \
     [-BB_H + BB_CH * (r + 1) - 5 for r in range(4)]


# ---- sprites (4-bit patterns) ----
# pens in every sprite palette: 0 transparent, 1 the ball, 2 white, 3 the
# shadow, 4 the paddle, 5 the capsule's pink, 6 black, 7 and 8 per palette
# (a capsule's colour; a brick's lit and shaded colours)
SP_BALL, SP_WHITE, SP_SHADOW, SP_PADDLE, SP_PINK, SP_BLACK, SP_VAR, SP_VAR2 = range(1, 9)
SPAL_PLOUGH, SPAL_CAP, SPAL_BRICK = 1, 2, 6      # 2..5: capsules W S L P; 6..14: bricks red..bonus


def brick_pen(t):
    """render.c's brick_pen(type, 1) (the bits' colours), type 1..9"""
    if 1 <= t <= 6:
        return 11 + 2 * (t - 1)
    return {7: 24, 8: 25, 9: 29}[t]


def sprite_palette():
    base = base_palette()
    c9 = lambda pen: base[pen]
    pal = []
    for k in range(16):
        e = [0, c9(PEN_BALL), c9(1), c9(2), c9(PEN_PADDLE), c9(PEN_CAP), c9(0), 0, 0] + [0] * 7
        if k == SPAL_PLOUGH:
            e[SP_BALL] = e[SP_PADDLE] = c9(PEN_RED)
        if SPAL_CAP <= k < SPAL_CAP + 4:
            e[SP_VAR] = c9((PEN_YELLOW, PEN_CYAN, PEN_GREEN, PEN_RED)[k - SPAL_CAP])
        if SPAL_BRICK <= k < SPAL_BRICK + 9:
            p = brick_pen(k - SPAL_BRICK + 1)
            e[SP_VAR], e[SP_VAR2] = c9(p), c9(p + 1)
        pal += e
    return pal


def raster(w, h, polys, ss=4):
    """polys: [(pen, [(x, y), ...]), ...] in pixel units, drawn in order;
    a pixel takes a polygon's pen if most of its samples are inside"""
    out = np.zeros((h, w), np.uint8)
    o = (np.arange(ss) + 0.5) / ss
    for pen, pts in polys:
        cnt = np.zeros((h, w), np.int32)
        for sy in o:
            for sx in o:
                X, Y = np.meshgrid(np.arange(w) + sx, np.arange(h) + sy)
                inside = np.ones((h, w), bool)
                n = len(pts)
                area = sum(pts[i][0] * pts[(i + 1) % n][1] - pts[(i + 1) % n][0] * pts[i][1] for i in range(n))
                sgn = 1 if area > 0 else -1
                for i in range(n):
                    (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % n]
                    inside &= sgn * ((x1 - x0) * (Y - y0) - (y1 - y0) * (X - x0)) >= 0
                cnt += inside
        out[cnt * 2 > ss * ss] = pen
    return out


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


OCT = [(10, 0), (7, 7), (0, 10), (-7, 7), (-10, 0), (-7, -7), (0, -10), (7, -7)]


def ball_pattern(r):
    k = r / 10.0
    body = [(8 + x * k, 8 - y * k) for x, y in OCT]
    hi = [(8 + (-3 + x / 3) * k, 8 - (3 + y / 3) * k) for x, y in OCT]
    return raster(16, 16, [(SP_BALL, body), (SP_WHITE, hi)])


def to4(img):
    """an h x 16 array of pens -> 4-bit pattern bytes (left pixel high)"""
    return bytes(int(img[y, x]) << 4 | int(img[y, x + 1]) for y in range(16) for x in range(0, 16, 2))


def tiles(img, cx, cy):
    """cover a picture's pixels with 16 x 16 tiles placed greedily (each at
    the first pixel not yet covered, kept inside the picture's bounds):
    (dx, dy, pattern image), dx dy the tile's top-left from (cx, cy)"""
    ys, xs = np.nonzero(img)
    x1, y1 = xs.max() + 1, ys.max() + 1
    left = img != 0
    out = []
    while left.any():
        y, x = [int(v[0]) for v in np.nonzero(left)]
        x, y = min(x, x1 - 16), min(y, y1 - 16)
        t = np.zeros((16, 16), np.uint8)
        part = img[y:y + 16, x:x + 16]
        t[:part.shape[0], :part.shape[1]] = part
        left[y:y + 16, x:x + 16] = False
        out.append((x - cx, y - cy, t))
    return out


def scale(z):
    return FOV / (z - CAM_Z)


def main():
    os.makedirs(os.path.join(ROOT, "gfx"), exist_ok=True)
    img = tunnel()
    data = bytes(int(img[y, x]) for x in range(320) for y in range(256))
    nextpic.write_banks(data, L2_BANK, os.path.join(ROOT, "gfx", "tunnel"), asm_file=False)
    l2pal = l2_palette()

    pats = []            # 128-byte patterns
    def add(p):
        b = to4(p)
        pats.append(b)
        return len(pats) - 1

    # the ball: radius classes 2.5 .. 8 pixels
    ball_r = [2.5 + 0.5 * k for k in range(12)]
    ball_pat0 = len(pats)
    for r in ball_r:
        add(ball_pattern(r))

    # the shadow: render.c's quad on the floor (x +-9, z +-9 at y -BB_H + 2), seen at x = 0
    shadow_z = [-60, -30, 0, 40, 80, 130, 190, 260, 340, 430, 530, 650, 800]
    shadow_pat0, shadow_dy = len(pats), []
    for z in shadow_z:
        yn, yf = (BB_H - 2) * scale(z - 9), (BB_H - 2) * scale(z + 9)
        wn, wf = 9 * scale(z - 9), 9 * scale(z + 9)
        mid = (yn + yf) / 2
        shadow_dy.append(int(round(mid)))                 # the centre's offset below the horizon
        frac = mid - round(mid)
        pts = [(8 - wf, 8 + yf - mid + frac), (8 + wf, 8 + yf - mid + frac), (8 + wn, 8 + yn - mid + frac), (8 - wn, 8 + yn - mid + frac)]
        add(raster(16, 16, [(SP_SHADOW, pts)]))

    # capsules: render.c's two cards (32 x 20 pink, 22 x 10 the capsule's colour)
    cap_z = [-60, -20, 30, 90, 170, 270, 400, 560, 740]
    cap_cls = []          # (first pattern, tiles (1 or 4), half size of the image)
    for z in cap_z:
        s = scale(z)
        big = 32 * s > 16 or 20 * s > 16
        n = 32 if big else 16
        c = n / 2
        im = raster(n, n, [(SP_PINK, rect(c - 16 * s, c - 10 * s, c + 16 * s, c + 10 * s)),
                           (SP_VAR, rect(c - 11 * s, c - 5 * s, c + 11 * s, c + 5 * s))])
        first = len(pats)
        if big:
            for ty in (0, 16):
                for tx in (0, 16):
                    add(im[ty:ty + 16, tx:tx + 16])
        else:
            add(im)
        cap_cls.append((first, 4 if big else 1, int(c)))

    # the capsules' letters (render.c's capsule_letters: W S L P in black,
    # drawn over everything), the SDK's 5x7 font
    font = parse_font5()
    letter_pat0 = len(pats)
    for ch in "WSLP":
        im = np.zeros((16, 16), np.uint8)
        g = font[ord(ch) - 33]
        for r in range(7):
            for c in range(5):
                if g[r] & (0x80 >> c):
                    im[5 + r, 6 + c] = SP_BLACK
        add(im)

    # the bits of broken bricks: render.c's 8 x 8 cards, lit and shaded
    part_sz = [2, 3, 4, 6]
    part_pat0 = len(pats)
    for sz in part_sz:
        for pen in (SP_VAR, SP_VAR2):
            im = np.zeros((16, 16), np.uint8)
            o = 8 - sz // 2
            im[o:o + sz, o:o + sz] = pen
            add(im)

    # the paddle: render.c's frame (four bars 5 thick and a cross) at z = 0
    s0 = scale(0)
    paddles = []
    pad0 = len(pats)
    for w, h in ((BB_PAD_W, BB_PAD_H), (BB_WIDE_W, BB_WIDE_H)):
        W, H = int(np.ceil((w * s0 + 1) / 16)) * 32, int(np.ceil((h * s0 + 1) / 16)) * 32
        cx, cy = W // 2, H // 2
        t = 5
        P = lambda x0, y0, x1, y1: rect(cx + x0 * s0, cy - y1 * s0, cx + x1 * s0, cy - y0 * s0)
        im = raster(W, H, [(SP_PADDLE, P(-w, h - t, w, h)), (SP_PADDLE, P(-w, -h, w, -h + t)),
                           (SP_PADDLE, P(-w, -h, -w + t, h)), (SP_PADDLE, P(w - t, -h, w, h)),
                           (SP_PADDLE, P(-6, -1, 6, 1)), (SP_PADDLE, P(-1, -6, 1, 6))])
        lst = []
        for dx, dy, tile in tiles(im, cx, cy):
            idx, flags = None, 0
            # a tile the paddles already have, maybe mirrored (sprite flags)
            for fl, tt in ((0, tile), (8, tile[:, ::-1]), (4, tile[::-1, :]), (12, tile[::-1, ::-1])):
                bb = to4(np.ascontiguousarray(tt))
                if bb in pats[pad0:]:
                    idx, flags = pad0 + pats[pad0:].index(bb), fl
                    break
            if idx is None:
                idx = add(tile)
            lst.append((dx, dy, idx, flags))
        paddles.append(lst)

    npat = len(pats)
    assert npat <= 128 - DYN_SLOTS, npat
    spr = b"".join(pats) + bytes(128 * (128 - npat))
    open(os.path.join(ROOT, "gfx", "spr.bin"), "wb").write(spr[:16384])

    # the size classes by depth: entry (z - ZMIN) / 4
    nz = (ZMAX - ZMIN) // 4 + 1
    zball, zshadow, zcap, zpart, zband, zscale = [], [], [], [], [], []
    for k in range(nz):
        z = ZMIN + 4 * k
        r = BB_R * scale(z)
        zball.append(min(range(len(ball_r)), key=lambda i: abs(ball_r[i] - r)))
        zshadow.append(min(range(len(shadow_z)), key=lambda i: abs(shadow_z[i] - z)))
        zcap.append(min(range(len(cap_z)), key=lambda i: abs(cap_z[i] - z)))
        ps = 8 * scale(z)
        zpart.append(min(range(len(part_sz)), key=lambda i: abs(part_sz[i] - ps)))
    for k in range((ZMAX - ZMIN) // 2 + 1):
        z = ZMIN + 2 * k
        zband.append(band_of(z))
        zscale.append(int(round(4096 * scale(z))))

    # ---- C ----
    def arr(t, name, vals, per=16):
        rows = [", ".join(str(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
        return f"const {t} {name}[{len(vals)}] = {{\n    " + ",\n    ".join(rows) + "\n};\n"

    h = [GEN, "#ifndef GFX_DATA_H", "#define GFX_DATA_H", "#include <stdint.h>",
         f"#define L2_BANK {L2_BANK}             /* the tunnel picture (5 banks) */",
         f"#define FIRST_BAND_PEN {FIRST_BAND_PEN}      /* band b's pens: + 3b floor, + 3b + 1 ceiling, + 3b + 2 sides */",
         f"#define NBANDS {NB}",
         f"#define ZT_MIN {ZMIN}            /* the depth tables: zt_*[(z - ZT_MIN) >> 2], zt_band/zt_scale[(z - ZT_MIN) >> 1] */",
         f"#define ZT_MAX {ZMAX}",
         f"#define NBZ {len(BZ)}",
         f"#define NBX {len(BX)}",
         f"#define NBY {len(BY)}",
         f"#define SPR_BALL {ball_pat0}",
         f"#define SPR_SHADOW {shadow_pat0}",
         f"#define SPR_LETTER {letter_pat0}       /* W S L P */",
         f"#define SPR_PART {part_pat0}         /* size k: + 2k lit, + 2k + 1 shaded */",
         f"#define SPR_NPAT {npat}",
         f"#define SPR_DYN {128 - DYN_SLOTS}          /* patterns made each frame (masked) */",
         f"#define NCAPCLS {len(cap_cls)}",
         f"#define SPAL_PLOUGH {SPAL_PLOUGH}",
         f"#define SPAL_CAP {SPAL_CAP}",
         f"#define SPAL_BRICK {SPAL_BRICK}",
         f"#define PADDLE_TILES {len(paddles[0])}",
         f"#define WIDE_TILES {len(paddles[1])}",
         "typedef struct { int8_t dx, dy; uint8_t pat, flags; } PadTile;",
         "typedef struct { uint8_t pat, n, half; } CapCls;",
         "extern const uint16_t l2_pal[256], spr_pal[256];",
         "extern const uint8_t band_par[NBANDS];",
         "extern const int16_t bz_tab[NBZ], bx_tab[NBX], by_tab[NBY];",
         "extern const int16_t sx_tab[NBZ][NBX], sy_tab[NBZ][NBY];",
         "extern const uint8_t zt_ball[], zt_shadow[], zt_cap[], zt_part[], zt_band[];",
         "extern const int8_t shadow_dy[];",
         "extern const uint16_t zt_scale[];",
         "extern const CapCls cap_cls[NCAPCLS];",
         "extern const PadTile pad_tiles[PADDLE_TILES], wide_tiles[WIDE_TILES];",
         "#endif", ""]
    open(os.path.join(ROOT, "src", "gfx_data.h"), "w").write("\n".join(h))
    c = [GEN, '#include "gfx_data.h"', ""]
    c.append(arr("uint16_t", "l2_pal", l2pal))
    c.append(arr("uint16_t", "spr_pal", sprite_palette()))
    c.append(arr("uint8_t", "band_par", [stripe(EDGES[b]) for b in range(NB)]))
    c.append(arr("int16_t", "bz_tab", BZ))
    c.append(arr("int16_t", "bx_tab", BX))
    c.append(arr("int16_t", "by_tab", BY))
    c.append("const int16_t sx_tab[NBZ][NBX] = {\n" + ",\n".join(
        "    { " + ", ".join(str(proj_x(x, z)) for x in BX) + " }" for z in BZ) + "\n};\n")
    c.append("const int16_t sy_tab[NBZ][NBY] = {\n" + ",\n".join(
        "    { " + ", ".join(str(proj_y(y, z)) for y in BY) + " }" for z in BZ) + "\n};\n")
    c.append(arr("uint8_t", "zt_ball", zball, 32))
    c.append(arr("uint8_t", "zt_shadow", zshadow, 32))
    c.append(arr("int8_t", "shadow_dy", shadow_dy))
    c.append(arr("uint8_t", "zt_cap", zcap, 32))
    c.append(arr("uint8_t", "zt_part", zpart, 32))
    c.append(arr("uint8_t", "zt_band", zband, 32))
    c.append(arr("uint16_t", "zt_scale", zscale))
    c.append("const CapCls cap_cls[NCAPCLS] = {\n" + ",\n".join(f"    {{ {a}, {b}, {d} }}" for a, b, d in cap_cls) + "\n};\n")
    for name, lst in (("pad_tiles", paddles[0]), ("wide_tiles", paddles[1])):
        c.append(f"const PadTile {name}[{len(lst)}] = {{\n" + ",\n".join(
            f"    {{ {dx}, {dy}, {p}, {f} }}" for dx, dy, p, f in lst) + "\n};\n")
    open(os.path.join(ROOT, "src", "gfx_data.c"), "w").write("\n".join(c))
    print(f"bands {NB}, sprite patterns {npat} (+{DYN_SLOTS} made each frame), paddle tiles {len(paddles[0])}/{len(paddles[1])}")


GEN = "/* generated by tools/gen_gfx.py - do not edit */"
DYN_SLOTS = 24


def parse_font5():
    import re
    src = open(os.path.join(ROOT, "..", "..", "sdk", "src", "font5.c")).read()
    return [[int(v, 16) for v in r.split(", ")] for r in
            re.findall(r"\{ (0x[0-9A-F]{2}(?:, 0x[0-9A-F]{2}){6}) \}", src)]


if __name__ == "__main__":
    main()
