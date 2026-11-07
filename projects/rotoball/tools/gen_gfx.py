#!/usr/bin/env python3
"""Rotoball's pictures for the Spectrum Next, made from the original's
renderer (amiga_mcp/examples/rotoball/pitch.c) on the host.

The pitch: the original redraws its chequered floor for each camera every
frame. Here Layer 2 (320 x 256, the original's screen) holds one fixed
picture, and the cameras move by palette:

  * each floor pixel's index is its sideways phase: where it falls in a
    two-cell period across the pitch, relative to the camera (120 steps a
    pane: indices 0..119 the top pane, 120..239 the bottom one). The
    camera's own sideways position only shifts which indices are light and
    which dark, so the program rotates a 120-entry palette run.
  * whether a row starts light or dark depends on how far down the pitch it
    looks (the camera's x); the copper switches that row between the first
    and the second Layer 2 palette (the second is the first inverted).

Sprites: the ball (the original's three-tone sphere, radius 3..31), the
skimmer (its triangle and hover skirt, scale 6..40; half and quarter
resolution with hardware zoom above 15 and 31) and the goal posts (1-pixel
lines 2..16 long, zoomed), drawn with the original's integer arithmetic.

    python3 tools/gen_gfx.py      (from projects/rotoball; writes gfx/ and
                                   src/gfx_data.asm, src/gfx_data.c/.h)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(PROJ, "..", "..", "sdk", "tools"))
import numpy as np  # noqa: E402
from nextpic import TRANSPARENT, group_patterns, to333  # noqa: E402

# ---- the original's geometry (rotoball.h, pitch.c)
SCR_W, SCR_H, PANE_H = 320, 256, 128
HORIZON_Y = PANE_H // 3                 # 42
FOCAL = 180
HOVER = 2
GRID = 5

N = 120                                 # sideways phases a pane
L2_BANK = 16                            # Layer 2: banks 16..20
SPR_BANK = 21                           # sprite images from bank 21 (page 42)

# palette indices on Layer 2
I_BLACK, I_SKY_TOP, I_SKY_BOT = 240, 241, 242

# the original's palette (main.c set_palette)
RGB = {
    "black": (0, 0, 0), "sky_top": (30, 20, 90), "sky_bot": (110, 60, 130),
    "grid_a": (20, 20, 50), "grid_b": (180, 180, 200),
    "goal_p1": (40, 200, 255), "goal_p2": (255, 180, 40),
    "ball_dark": (90, 10, 10), "ball": (220, 40, 30), "ball_hi": (255, 200, 160),
    "roto_p1": (40, 200, 255), "roto_p2": (255, 180, 40), "edge": (20, 20, 40),
    "hud_bg": (10, 10, 25), "hud_fg": (220, 220, 240), "white": (255, 255, 255),
}

# sprite pattern pens (8-bit patterns; palette offset 1 = +16 for P2's colours)
S_EDGE, S_BODY, S_BDARK, S_BALL, S_BHI, S_POST = 1, 2, 3, 4, 5, 6


def rgb9(c):
    r, g, b = (int(v) for v in to333(c))
    return r << 6 | g << 3 | b


def cdiv(a, b):
    """C's integer division (truncates towards zero)"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def isqrt_floor(v):
    """the original's `w = 0; while ((w+1)*(w+1) <= v) w++;`"""
    w = 0
    while (w + 1) * (w + 1) <= v:
        w += 1
    return w


# ---------------------------------------------------------------- the pitch
def zdist(dy):
    """port: the original's zdist = 360 / dy in integers; exact here"""
    return HOVER * FOCAL / dy


def l2_picture():
    img = np.zeros((SCR_H, SCR_W), np.uint8)
    for pane, (y0, fwd, base, sky) in enumerate(((0, -1, 0, I_SKY_TOP), (PANE_H, 1, N, I_SKY_BOT))):
        img[y0:y0 + HORIZON_Y, :] = sky
        img[y0 + HORIZON_Y, :] = I_BLACK
        for y in range(y0 + HORIZON_Y + 1, y0 + PANE_H):
            zd = zdist(y - (y0 + HORIZON_Y))
            for x in range(SCR_W):
                dz = -fwd * (x - SCR_W // 2) * zd / FOCAL     # world z offset from the camera
                w = (dz / (2 * GRID)) % 1.0
                img[y, x] = base + min(N - 1, int(w * N))
    img[PANE_H - 1:PANE_H + 1, :] = I_BLACK                   # the divider
    return img


# ---------------------------------------------------------------- sprites
def ball_image(r):
    """draw_ball_sphere at (r, r)"""
    size = 2 * r + 1
    img = np.full((size, size), TRANSPARENT, np.uint8)
    cx = cy = r
    hi_dx = hi_dy = cdiv(-r, 3)
    r2, mr = r * r, cdiv(r * 5, 6)
    mr2, hr = mr * mr, cdiv(r, 3)
    hr2 = hr * hr
    for dy in range(-r, r + 1):
        y = cy + dy
        hw_out = r2 - dy * dy
        if hw_out <= 0:
            continue
        hw_mid = mr2 - dy * dy
        w_out = isqrt_floor(hw_out)
        w_mid = isqrt_floor(hw_mid) if hw_mid > 0 else 0
        img[y, cx - w_out:cx + w_out + 1] = S_BDARK
        if w_mid > 0:
            img[y, cx - w_mid:cx + w_mid + 1] = S_BALL
        hdy = dy - hi_dy
        hw_hi = hr2 - hdy * hdy
        if hw_hi > 0:
            w_hi = isqrt_floor(hw_hi)
            img[y, cx + hi_dx - w_hi:cx + hi_dx + w_hi + 1] = S_BHI
    return img


def skimmer_image(s):
    """draw_skimmer with the ground point at (s + 1, 3 s): apex at row 0"""
    w, h = 2 * s + 3, 3 * s + 2
    img = np.full((h, w), TRANSPARENT, np.uint8)
    gx, gy = s + 1, 3 * s
    span_h = 3 * s
    for dy in range(span_h + 1):                       # outline
        half_w = ((dy + 1) * s) // span_h
        img[dy, gx - half_w - 1:gx + half_w + 2] = S_EDGE
    for dy in range(1, span_h + 1):                    # body
        half_w = (dy * s) // span_h
        img[dy, gx - half_w:gx + half_w + 1] = S_BODY
    img[gy, gx - s - 1:gx + s + 2] = S_EDGE            # hover skirt
    img[gy + 1, gx - s + 1:gx + s] = S_EDGE
    return img


def shrink(img, z):
    """1/z resolution for hardware zoom z: a block shows its outline if it has
    any, else its body (so the 1-pixel edge survives)"""
    if z == 1:
        return img
    h, w = img.shape
    out = np.full(((h + z - 1) // z, (w + z - 1) // z), TRANSPARENT, np.uint8)
    for y in range(out.shape[0]):
        for x in range(out.shape[1]):
            blk = img[y * z:(y + 1) * z, x * z:(x + 1) * z]
            for pen in (S_EDGE, S_BODY):
                if (blk == pen).any():
                    out[y, x] = pen
                    break
    return out


def skimmer_zoom(s):
    return 1 if s <= 15 else 2 if s <= 31 else 4


def post_patterns():
    """8 patterns: a 1-pixel line at x 0, 2, 4 .. 16 pixels long, at the bottom"""
    out = bytearray()
    for n in range(2, 17, 2):
        p = np.full((16, 16), TRANSPARENT, np.uint8)
        p[16 - n:, 0] = S_POST
        out += p.tobytes()
    return bytes(out)


class Packer:
    """images into 8K pages without crossing a page (a page maps into one MMU slot)"""

    def __init__(self, first_page):
        self.first = first_page
        self.pages = [bytearray()]

    def add(self, data):
        assert len(data) <= 8192
        if len(self.pages[-1]) + len(data) > 8192:
            self.pages.append(bytearray())
        page = len(self.pages) - 1
        addr = len(self.pages[-1])
        self.pages[-1] += data
        return self.first + page, 0x4000 + addr       # read through slot 2

    def banks(self):
        data = bytearray()
        for p in self.pages:
            data += p + bytes(8192 - len(p))
        if len(self.pages) % 2:
            data += bytes(8192)
        return bytes(data)


def main():
    gfx = os.path.join(PROJ, "gfx")
    src = os.path.join(PROJ, "src")
    os.makedirs(gfx, exist_ok=True)

    # Layer 2, column-major
    pic = l2_picture()
    l2 = pic.T.tobytes()
    asm = ["; generated by tools/gen_gfx.py - do not edit: the pitch (Layer 2) and the",
           "; sprite images, straight into Next memory banks by the .nex", ""]
    for k in range(5):
        name = f"gfx/l2_{k}.bin"
        open(os.path.join(PROJ, name), "wb").write(l2[k * 16384:(k + 1) * 16384])
        asm += [f"SECTION BANK_{L2_BANK + k}", f'BINARY "{name}"']

    pk = Packer(SPR_BANK * 2)
    balls = {}
    for r in range(3, 32):
        data, cols, rows = group_patterns(ball_image(r))
        assert cols == rows <= 4
        balls[r] = pk.add(data) + (cols,)
    skims = {}
    for s in range(6, 41):
        z = skimmer_zoom(s)
        data, cols, rows = group_patterns(shrink(skimmer_image(s), z))
        assert cols <= 3 and rows <= 3, (s, cols, rows)
        skims[s] = pk.add(data) + (cols, rows, z)
    posts = pk.add(post_patterns())
    spr = pk.banks()
    nb = len(spr) // 16384
    for k in range(nb):
        name = f"gfx/spr_{k}.bin"
        open(os.path.join(PROJ, name), "wb").write(spr[k * 16384:(k + 1) * 16384])
        asm += [f"SECTION BANK_{SPR_BANK + k}", f'BINARY "{name}"']
    open(os.path.join(src, "gfx_data.asm"), "w").write("\n".join(asm) + "\n")

    # tables
    zc = [0] + [round(zdist(dy) * 256 / GRID) for dy in range(1, PANE_H - HORIZON_Y)]
    h = ["/* generated by tools/gen_gfx.py - do not edit */",
         "#ifndef GFX_DATA_H", "#define GFX_DATA_H", "#include <stdint.h>", "",
         f"#define L2_BANK {L2_BANK}",
         f"#define FLOOR_N {N}                  /* sideways phases a pane (palette entries) */",
         f"#define I_BLACK {I_BLACK}", f"#define I_SKY_TOP {I_SKY_TOP}", f"#define I_SKY_BOT {I_SKY_BOT}",
         f"#define RGB_GRID_A {rgb9(RGB['grid_a'])}", f"#define RGB_GRID_B {rgb9(RGB['grid_b'])}",
         f"#define RGB_BLACK {rgb9(RGB['black'])}", f"#define RGB_SKY_TOP {rgb9(RGB['sky_top'])}",
         f"#define RGB_SKY_BOT {rgb9(RGB['sky_bot'])}",
         f"#define RGB_HUD_BG {rgb9(RGB['hud_bg'])}", f"#define RGB_HUD_FG {rgb9(RGB['hud_fg'])}",
         f"#define RGB_GOAL_P1 {rgb9(RGB['goal_p1'])}", f"#define RGB_GOAL_P2 {rgb9(RGB['goal_p2'])}",
         f"#define RGB_WHITE {rgb9(RGB['white'])}",
         "",
         "/* a floor row's distance (dy rows below the horizon) in 1/256 grid cells */",
         f"extern const int16_t floor_zc[{len(zc)}];",
         "typedef struct { uint8_t page; uint16_t addr; uint8_t cols; } BallImg;",
         "typedef struct { uint8_t page; uint16_t addr; uint8_t cols, rows, zoom; } SkimImg;",
         "extern const BallImg ball_img[32];           /* radius 3..31 (top-left = centre - r) */",
         "extern const SkimImg skim_img[41];           /* scale 6..40 (top-left = ground - (s+1, 3s)) */",
         f"#define POST_PAGE {posts[0]}",
         f"#define POST_ADDR 0x{posts[1]:04X}               /* 8 patterns: a line 2, 4 .. 16 long */",
         "extern const uint16_t spr_pal[32];",
         "#endif", ""]
    open(os.path.join(src, "gfx_data.h"), "w").write("\n".join(h))

    def row(v):
        return ", ".join(str(x) for x in v)
    c = ["/* generated by tools/gen_gfx.py - do not edit */", '#include "gfx_data.h"', "",
         f"const int16_t floor_zc[{len(zc)}] = {{"]
    c += [f"    {row(zc[i:i + 12])}," for i in range(0, len(zc), 12)]
    c += ["};", "", "const BallImg ball_img[32] = {"]
    for r in range(32):
        p, a, n = balls.get(r, (0, 0, 0))
        c.append(f"    {{ {p}, 0x{a:04X}, {n} }},   /* r {r} */")
    c += ["};", "", "const SkimImg skim_img[41] = {"]
    for s in range(41):
        p, a, cl, rw, z = skims.get(s, (0, 0, 0, 0, 0))
        c.append(f"    {{ {p}, 0x{a:04X}, {cl}, {rw}, {z} }},   /* s {s} */")
    pal = [0] * 32
    for i, (k1, k2) in {S_EDGE: ("edge", "edge"), S_BODY: ("roto_p1", "roto_p2"),
                        S_BDARK: ("ball_dark", "ball_dark"), S_BALL: ("ball", "ball"),
                        S_BHI: ("ball_hi", "ball_hi"), S_POST: ("goal_p1", "goal_p2")}.items():
        pal[i], pal[16 + i] = rgb9(RGB[k1]), rgb9(RGB[k2])
    c += ["};", "", "/* sprites: pens 1..6, P2's colours at +16 (palette offset 1) */",
          f"const uint16_t spr_pal[32] = {{ {row(pal[:16])},", f"    {row(pal[16:])} }};", ""]
    open(os.path.join(src, "gfx_data.c"), "w").write("\n".join(c))

    # a preview of the picture with a camera at the kick-off spots (for the README / checks)
    print(f"Layer 2: banks {L2_BANK}..{L2_BANK + 4}; sprites: {len(pk.pages)} pages from bank {SPR_BANK} "
          f"({nb} banks)")


if __name__ == "__main__":
    main()
