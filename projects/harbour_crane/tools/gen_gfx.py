#!/usr/bin/env python3
"""Harbour Crane's backdrop and palette for the Spectrum Next, from the
original's renderer (tools/orig/render.c: background() and the quay) and
camera (tools/geom.py).

The backdrop is the part of the picture that never moves: the sky (the
original's copper gradient, 16 bands), the far shore and its buildings, the
sea, and the quay with the line painted where containers wait. The Next
copies it into the back buffer every frame and draws the rest over it
(src/view.c).

The sea: the original draws it as bands 300 units deep, alternately dark
and light, that drift towards the camera with the swell (z + drift,
drift = (phase >> 12) * 300 / 102). Here each sea pixel holds where it is
on the water: indices 0..119 its z modulo 600 (5-unit steps), 120..179 its
z from -400 to -100 (where the bands' near edge passes by). The program
sets those palette entries each tick from the drift (src/view.c
sea_palette), so the bands move with no pixel drawn.

Palette (Layer 2): 0..179 the sea, 180..195 the sky's bands, 200 + pen for
the original's 32 pens (tools/orig/render.c).

    python3 tools/gen_gfx.py          (from projects/harbour_crane)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(PROJ, "..", "..", "sdk", "tools"))
from PIL import Image, ImageDraw  # noqa: E402
import geom  # noqa: E402
from geom import VIEW_H, VIEW_W  # noqa: E402

BACK_BANK = 16                    # the backdrop: banks 16-18 (row-major 256 x 192)
SPR_BANK = 25                     # sprite patterns

PALETTE = [  # the original's crane_palette (12-bit)
    0x000, 0xFFF, 0x123, 0x99A, 0x7AD, 0x236, 0x358, 0x576, 0xBBA, 0x776,
    0xFC2, 0xA81, 0x23A, 0x125, 0x733, 0xEED, 0x333,
    0x5C5, 0x383, 0x48E, 0x259, 0xF93, 0xB52, 0xE44, 0x922,
    0x6CE, 0xF33, 0xFE4, 0x6F6, 0x5EF, 0x112, 0xDEF]
PEN0 = 200
SEA_MOD, SEA_NEAR, SKY0 = 0, 120, 180
PEN_SKY, PEN_SEA, PEN_SHORE, PEN_QUAY, PEN_YELLOW = 4, 5, 7, 8, 27
SKY_TOP, SKY_BOT, SKY_ROWS = 0x48C, 0xBDE, 140
WATER_Y, QUAY_X1, PICK_X = -70, 40, -140
SEA_FAR = 2600


def rgb9_12(c):
    """12-bit Amiga colour -> 9-bit RGB333 (nearest)"""
    r, g, b = (c >> 8) & 15, (c >> 4) & 15, c & 15
    return (round(r * 7 / 15) << 6) | (round(g * 7 / 15) << 3) | round(b * 7 / 15)


def rgb8_12(c):
    """12-bit Amiga colour -> the nearest of the Next's 8-bit RRRGGGBB"""
    r, g, b = ((c >> 8) & 15) / 15, ((c >> 4) & 15) / 15, (c & 15) / 15
    best = min(range(256), key=lambda v: ((v >> 5) / 7 - r) ** 2 + (((v >> 2) & 7) / 7 - g) ** 2
               + ((v & 3) / 3 - b) ** 2)
    return best


def mix(c, to, num, den):
    """main_68k.c mix(): the sky gradient's steps"""
    r, g, b = (c >> 8) & 15, (c >> 4) & 15, c & 15
    r += int(((((to >> 8) & 15) - r) * num) / den)
    g += int(((((to >> 4) & 15) - g) * num) / den)
    b += int((((to & 15) - b) * num) / den)
    return (r << 8) | (g << 4) | b


def orig_row(y):
    """the original's screen row of the Next's view row y"""
    return 12 + y / 0.8


class Painter:
    """render.c's quad() into a list sorted like a3_flush (far first, then in order)"""

    def __init__(self, far_z):
        self.polys = []
        self.far_z = far_z

    def bucket(self, depth):
        """a3d_draw.c bucket_of: 512 buckets over 0..far_z"""
        return 0 if depth < 0 else 511 if depth >= self.far_z else min(511, depth * 512 // self.far_z)

    def quad(self, pts, bias, col):
        v = [geom.xform(*p) for p in pts]
        depth = (sum(p[2] for p in v) >> 2) + bias
        self.polys.append((self.bucket(depth), len(self.polys), [geom.project(p) for p in v], col))

    def box(self, x0, x1, y0, y1, z0, z1, lit, shade, bias):
        self.quad([(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)], bias, lit)
        self.quad([(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)], bias + 1, shade)
        if x1 < geom.CAM_X:
            self.quad([(x1, y0, z0), (x1, y1, z0), (x1, y1, z1), (x1, y0, z1)], bias + 1, shade)
        elif x0 > geom.CAM_X:
            self.quad([(x0, y0, z0), (x0, y1, z0), (x0, y1, z1), (x0, y0, z1)], bias + 1, shade)

    def draw(self, img):
        d = ImageDraw.Draw(img)
        for _, _, pts, col in sorted(self.polys, key=lambda p: (-p[0], p[1])):
            d.polygon(pts, fill=col)
        self.polys = []


SEA = 254                          # a marker while painting: a sea pixel


def backdrop():
    img = Image.new("L", (VIEW_W, VIEW_H))
    px = img.load()
    for y in range(VIEW_H):         # the sky: pen 4 under the copper gradient
        k = min(15, int(orig_row(y) * 16 / SKY_ROWS)) if orig_row(y) < SKY_ROWS else 15
        for x in range(VIEW_W):
            px[x, y] = SKY0 + k
    # background(): the far shore, its buildings, the sea (a3_begin(4000) list)
    p = Painter(4000)
    p.quad([(-2400, WATER_Y, SEA_FAR), (2600, WATER_Y, SEA_FAR), (2600, 40, SEA_FAR), (-2400, 40, SEA_FAR)], 0,
           PEN0 + PEN_SHORE)
    for k in range(9):
        x, h = -1700 + k * 420 + (k * 97 % 150), 70 + (k * 53 % 110)
        p.quad([(x, 30, 2590), (x + 140, 30, 2590), (x + 140, 30 + h, 2590), (x, 30 + h, 2590)], 0,
               PEN0 + PEN_SHORE + 2 * (k & 1))
    # the sea at its furthest drift and nearest start: every pixel it can cover
    for z in range(-400, SEA_FAR, 300):
        p.quad([(-2400, WATER_Y, z), (2600, WATER_Y, z), (2600, WATER_Y, z + 300 + 294),
                (-2400, WATER_Y, z + 300 + 294)], 0, SEA)
    p.draw(img)
    p = Painter(2000)
    # the quay (a3_begin(2000) list): its box, its top, the line where containers wait
    p.box(-700, QUAY_X1, WATER_Y - 30, 0, -120, 260, PEN0 + PEN_QUAY + 1, PEN0 + PEN_QUAY + 1, 0)
    p.quad([(-700, 0, -120), (QUAY_X1, 0, -120), (QUAY_X1, 0, 260), (-700, 0, 260)], 2, PEN0 + PEN_QUAY)
    p.quad([(PICK_X - 30, 1, -26), (PICK_X + 30, 1, -26), (PICK_X + 30, 1, 26), (PICK_X - 30, 1, 26)], -2,
           PEN0 + PEN_YELLOW)
    p.quad([(PICK_X - 27, 1, -23), (PICK_X + 27, 1, -23), (PICK_X + 27, 1, 23), (PICK_X - 27, 1, 23)], -3,
           PEN0 + PEN_QUAY)
    p.draw(img)
    # the sea's pixels: where on the water they are
    for y in range(VIEW_H):
        for x in range(VIEW_W):
            if px[x, y] != SEA:
                continue
            hit = geom.unproject_water(x, y, WATER_Y)
            z = hit[1] if hit else SEA_FAR
            if z < -400:
                px[x, y] = SKY0 + 15     # nearer than the sea's near edge: the sky (as the original)
            elif z < -100:
                px[x, y] = SEA_NEAR + min(59, int((z + 400) / 5))
            else:
                px[x, y] = SEA_MOD + min(119, int(((z + 400) % 600) / 5))
    return img


TOP = 320                          # HC_GANTRY_Y
LEGS = (-300, -40)
SPR_PAL = 15                       # the crane's sprites: 4-bit, palette offset 15 (pens 1, 2)


def crane_near():
    """the crane's near side (render.c crane(): the near legs, the crossbeams,
    the near rail, the brace) - fixed in the world, so the Next shows them as
    sprites over Layer 2: index 1 the crane's colour, 2 its shade, 0 clear"""
    img = Image.new("L", (VIEW_W, VIEW_H))
    p = Painter(2000)
    for x in LEGS:
        p.box(x - 8, x + 8, 0, TOP + 25, -52, -40, 1, 2, 0)
        p.box(x - 8, x + 8, TOP + 25, TOP + 37, -52, 52, 1, 2, 0)
    p.box(-320, 460, TOP + 10, TOP + 25, -52, -40, 1, 2, 0)
    p.box(-300, -40, 160, 172, -52, -40, 1, 2, 0)
    p.draw(img)
    # 16 x 16 cells on the sprite plane (Layer 2 is at 32, 32), identical ones shared
    pats, cells = [], []
    pxl = img.load()
    for cy in range(-32, VIEW_H, 16):
        for cx in range(-32, VIEW_W, 16):
            cell = bytes(pxl[x, y] if 0 <= x < VIEW_W and 0 <= y < VIEW_H else 0
                         for y in range(cy + 0, cy + 16) for x in range(cx, cx + 16))
            if not any(cell):
                continue
            if cell not in pats:
                pats.append(cell)
            cells.append((cx + 32, cy + 32, pats.index(cell)))
    data = bytearray()
    for c in pats:                   # 4-bit: two pixels a byte, the left one high
        data += bytes((c[i] << 4) | c[i + 1] for i in range(0, 256, 2))
    return data, cells


def palette():
    pal = [0] * 256
    for k in range(16):
        pal[SKY0 + k] = rgb9_12(mix(SKY_TOP, SKY_BOT, k, 15))
    for i, c in enumerate(PALETTE):
        pal[PEN0 + i] = rgb9_12(c)
    for i in range(180):
        pal[i] = rgb9_12(PALETTE[PEN_SEA])
    return pal


def main():
    img = backdrop()
    data = img.tobytes()
    gfx = os.path.join(PROJ, "gfx")
    os.makedirs(gfx, exist_ok=True)
    asm = ["; generated by tools/gen_gfx.py - do not edit: the backdrop (Layer 2 256 x 192,", "; banks "
           f"{BACK_BANK}-{BACK_BANK + 2}), straight into Next memory by the .nex", ""]
    for k in range(3):
        name = f"gfx/back_{k}.bin"
        open(os.path.join(PROJ, name), "wb").write(data[k * 16384:(k + 1) * 16384])
        asm += [f"SECTION BANK_{BACK_BANK + k}", f'BINARY "{name}"']
    spr, cells = crane_near()
    assert len(spr) <= 64 * 256 // 2 and len(cells) <= 100, (len(spr), len(cells))
    open(os.path.join(gfx, "crane_spr.bin"), "wb").write(spr)
    asm += [f"SECTION BANK_{SPR_BANK}", 'BINARY "gfx/crane_spr.bin"']
    open(os.path.join(PROJ, "src", "gfx_data.asm"), "w").write("\n".join(asm) + "\n")
    pal = palette()
    rows = [", ".join(str(c) for c in pal[i:i + 16]) for i in range(0, 256, 16)]
    open(os.path.join(PROJ, "src", "gfx_data.c"), "w").write(
        "/* generated by tools/gen_gfx.py - do not edit */\n#include <stdint.h>\n#include \"gfx_data.h\"\n\n"
        "/* Layer 2: 0..179 the sea (set each tick), 180..195 the sky, 200 + the original's pens */\n"
        "const uint16_t l2_pal[256] = {\n    " + ",\n    ".join(rows) + "\n};\n\n"
        f"const uint16_t sea_rgb[2] = {{ {rgb9_12(PALETTE[PEN_SEA])}, {rgb9_12(PALETTE[PEN_SEA + 1])} }};\n"
        f"const uint16_t sky_near_rgb = {pal[SKY0 + 15]};\n\n"
        "/* the crane's near side as 4-bit sprites (tools/gen_gfx.py crane_near) */\n"
        f"const int16_t crane_spr_x[{len(cells)}] = {{ " + ", ".join(str(c[0]) for c in cells) + " };\n"
        f"const uint8_t crane_spr_y[{len(cells)}] = {{ " + ", ".join(str(c[1]) for c in cells) + " };\n"
        f"const uint8_t crane_spr_p[{len(cells)}] = {{ " + ", ".join(str(c[2]) for c in cells) + " };\n")
    open(os.path.join(PROJ, "src", "gfx_data.h"), "w").write(
        "/* generated by tools/gen_gfx.py - do not edit */\n#ifndef GFX_DATA_H\n#define GFX_DATA_H\n#include <stdint.h>\n"
        f"#define BACK_BANK {BACK_BANK}\n#define PEN0 {PEN0}\n#define SEA_MOD {SEA_MOD}\n#define SEA_NEAR {SEA_NEAR}\n"
        f"#define SKY0 {SKY0}\n#define PANEL_RGB8 {rgb8_12(PALETTE[2])}\n"
        f"#define CAM_X {geom.CAM_X}\n#define CAM_Y {geom.CAM_Y}\n#define CAM_Z {geom.CAM_Z}\n"
        f"#define CAM_CP12 {geom.CP12}\n#define CAM_SP12 {geom.SP12}\n"
        f"#define VIEW_CX {geom.VIEW_CX}\n#define VIEW_CY {geom.VIEW_CY}\n#define VIEW_FOV {geom.FOV}\n"
        "extern const uint16_t l2_pal[256];\nextern const uint16_t sea_rgb[2];\nextern const uint16_t sky_near_rgb;\n"
        f"#define CRANE_SPRITES {len(cells)}\n#define CRANE_PATTERNS {len(spr) // 128}\n#define CRANE_SPR_PAL {SPR_PAL}\n"
        f"#define CRANE_PAT_PAGE {SPR_BANK * 2}            /* the patterns (4-bit, 128 bytes each) at 0x4000 in slot 2 */\n"
        "extern const int16_t crane_spr_x[CRANE_SPRITES];\n"
        "extern const uint8_t crane_spr_y[CRANE_SPRITES], crane_spr_p[CRANE_SPRITES];\n"
        "#endif\n")
    # a preview with the sea at drift 0
    prev = Image.new("RGB", img.size)
    pp, ip = prev.load(), img.load()

    def rgb(c9):
        return (((c9 >> 6) & 7) * 255 // 7, ((c9 >> 3) & 7) * 255 // 7, (c9 & 7) * 255 // 7)
    for y in range(VIEW_H):
        for x in range(VIEW_W):
            i = ip[x, y]
            if i < 120:
                c = sea_rgb_at((i * 5), 0)
            elif i < 180:
                c = sea_rgb_at((i - 120) * 5, 0, near=True)
            else:
                c = pal[i]
            pp[x, y] = rgb(c)
    prev.resize((512, 384), Image.NEAREST).save(os.path.join(PROJ, "build", "backdrop_preview.png"))
    print(f"backdrop: banks {BACK_BANK}..{BACK_BANK + 2}")


def sea_rgb_at(zq, drift, near=False):
    """the colour a sea index shows (src/view.c sea_palette)"""
    if near:
        if zq < drift:
            return rgb9_12(mix(SKY_TOP, SKY_BOT, 15, 15))
        return rgb9_12(PALETTE[PEN_SEA + (((zq - drift) // 300) & 1)])
    return rgb9_12(PALETTE[PEN_SEA + (((zq - drift) // 300) & 1)])


if __name__ == "__main__":
    os.makedirs(os.path.join(PROJ, "build"), exist_ok=True)
    main()
