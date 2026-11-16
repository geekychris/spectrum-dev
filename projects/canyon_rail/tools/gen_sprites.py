#!/usr/bin/env python3
"""Canyon Rail's sprites: the original's models (render.c render_init: boxes
and a prism, shaded as the engine's mdl_face shades them) and its
camera-facing shapes (pods, bolts, the explosions' stars), drawn on the host
at a ladder of sizes as 4-bit 16 x 16 sprite patterns.

Sizes: size k is F0 * RATIO**k pixels per unit across (the picture's 4/5
scale of the original's 320-wide view; 3/4 down: render.h). An image wider
or taller than 32 pixels is drawn at half (quarter, eighth) size and scaled
up by the sprite hardware.

Sprite colours (one 16-colour palette, index 0 transparent):
  1, 2 turret (lit, shaded)  3, 4 drone  5 pod  6 white  7 bolt  8 glow
  9, 10 guardian  11 node  12 red  13 fire  14 yellow  15 green (the sight)

Output: gfx/spr_*.bin and src/spr_data.asm (banks SPR_BANK..), src/spr_tab.c,
src/spr_views.h.   ./tools/gen_sprites.py [--show sheet.png]
"""
import argparse
import math
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
GFX = os.path.join(HERE, "..", "gfx")
SRC = os.path.join(HERE, "..", "src")

SPR_BANK = 40
NSIZES = 18
F0, RATIO = 5.4, 0.8            # pixels per unit (across) of size 0, the step
YS = 0.75 / 0.8                 # down : across (render.h)

# original pen -> sprite colour (render.c's PEN_*)
SPEN = {13: 1, 14: 2, 15: 3, 16: 4, 17: 5, 1: 6, 18: 7, 19: 8, 20: 9, 21: 10, 22: 11, 26: 12, 23: 13, 27: 14, 28: 15}
PAL12 = {1: 0x889, 2: 0x556, 3: 0x6CD, 4: 0x38A, 5: 0x9F4, 6: 0xFFF, 7: 0xF44, 8: 0xFA8, 9: 0x99B, 10: 0x667,
         11: 0xF5E, 12: 0xF33, 13: 0xFA3, 14: 0xFE4, 15: 0x6F6}


def a3_sin(a):
    return math.sin(a * 2 * math.pi / 65536)


class Model:
    def __init__(self):
        self.v, self.f = [], []

    def vtx(self, x, y, z):
        self.v.append((x, y, z))
        return len(self.v) - 1

    def face(self, lit, shade, idx, inner):
        """a3d_draw.c mdl_face: flipped if it faces the inside point; lit if
        -nx + 2 ny + nz is over 3/4 of |n|"""
        a, b, c = (self.v[i] for i in idx[:3])
        u = [b[k] - a[k] for k in range(3)]
        w = [c[k] - a[k] for k in range(3)]
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        if sum(n[k] * (a[k] - inner[k]) for k in range(3)) < 0:
            idx = list(reversed(idx))
            n = tuple(-x for x in n)
        light = -n[0] + 2 * n[1] + n[2]
        mag = sum(abs(x) for x in n)
        self.f.append((list(idx), lit if (mag and light * 4 > mag * 3) else shade, n))

    def box(self, lit, shade, x0, x1, y0, y1, z0, z1):
        b = len(self.v)
        inner = ((x0 + x1) // 2, (y0 + y1) // 2, (z0 + z1) // 2)
        for (x, y, z) in [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                          (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]:
            self.vtx(x, y, z)
        for q in [(0, 3, 2, 1), (5, 6, 7, 4), (4, 7, 3, 0), (1, 2, 6, 5), (3, 7, 6, 2), (0, 1, 5, 4)]:
            self.face(lit, shade, [b + i for i in q], inner)

    def prism(self, lit, shade, sides, r, y0, y1):
        b = len(self.v)
        cy = (y0 + y1) // 2
        for k in range(sides):
            a = k * 65536 // sides
            self.vtx(int(a3_sin(a) * r), y0, int(a3_sin(a + 16384) * r))
            self.vtx(int(a3_sin(a) * r), y1, int(a3_sin(a + 16384) * r))
        for k in range(sides):
            j = (k + 1) % sides
            self.face(lit, shade, [b + 2 * k, b + 2 * k + 1, b + 2 * j + 1, b + 2 * j], (0, cy, 0))
        c = self.vtx(0, y1, 0)
        for k in range(sides):
            j = (k + 1) % sides
            self.face(lit, lit, [b + 2 * k + 1, b + 2 * j + 1, c], (0, y0, 0))


def models():
    """render.c render_init"""
    m = {}
    t = m["turret"] = Model()
    t.box(13, 14, -14, 14, -12, 6, -14, 14)
    t.box(14, 14, -8, 8, 6, 14, -8, 8)
    t.box(26, 26, -3, 3, 8, 12, 8, 30)
    d = m["drone"] = Model()
    d.box(15, 16, -8, 8, -5, 5, -10, 12)
    d.box(16, 16, -24, 24, -1, 2, -6, 4)
    d.box(26, 26, -3, 3, -3, 3, 12, 15)
    c = m["core"] = Model()
    c.box(20, 21, -50, 50, -50, 50, -50, 50)
    c.box(26, 26, -18, 18, -18, 18, -56, -50)
    n = m["node"] = Model()
    n.prism(22, 26, 6, 18, -18, 18)
    return m


def rot(p, yaw, pitch=0.0):
    """a3_rot_ypr: yaw about y, then pitch about x (radians)"""
    x, y, z = p
    co, si = math.cos(yaw), math.sin(yaw)
    x, z = x * co + z * si, z * co - x * si
    cp, sp = math.cos(pitch), math.sin(pitch)
    y, z = y * cp - z * sp, y * sp + z * cp
    return x, y, z


def draw_model(model, f, yaw, pitch=0.0):
    """seen from in front (the camera at -z looking +z: the model's +z side
    faces it when yaw = 180 degrees), at f pixels per unit -> (image, ax, ay)"""
    D = 160 / f                         # the depth that gives f (the picture's FOV 160)
    pts = []
    for v in model.v:
        x, y, z = rot(v, yaw, pitch)
        zz = max(D + z, 4)
        pts.append((x * 160 / zz, -y * 160 / zz * YS, zz, (x, y, z)))
    faces = []
    for idx, col, n in model.f:
        nr = rot(n, yaw, pitch)
        p0 = pts[idx[0]][3]
        view = (-p0[0], -p0[1], -D - p0[2])
        if sum(nr[k] * view[k] for k in range(3)) <= 0:
            continue
        faces.append((sum(pts[i][2] for i in idx), [(pts[i][0], pts[i][1]) for i in idx], col))
    faces.sort(key=lambda t: -t[0])
    return paint(faces)


def paint(faces):
    xs = [x for _, poly, _ in faces for x, _ in poly] or [0]
    ys = [y for _, poly, _ in faces for _, y in poly] or [0]
    x0, x1 = math.floor(min(xs)), math.ceil(max(xs))
    y0, y1 = math.floor(min(ys)), math.ceil(max(ys))
    img = Image.new("P", (max(1, x1 - x0 + 1), max(1, y1 - y0 + 1)), 0)
    d = ImageDraw.Draw(img)
    for _, poly, col in faces:
        d.polygon([(x - x0, y - y0) for x, y in poly], fill=SPEN.get(col, 6))
    return img, -x0, -y0


def draw_squares(f, squares):
    """camera-facing squares (render.c spot): (half side in units, pen), first drawn first"""
    faces = []
    for k, (r, pen) in enumerate(squares):
        faces.append((-k, [(-r * f, -r * f * YS), (r * f, -r * f * YS), (r * f, r * f * YS), (-r * f, r * f * YS)], pen))
    return paint(faces)


def draw_star(f, t):
    """render.c burst at unit size: 8 points, r and r/2 alternating, turned by t"""
    pen = 27 if t > 7 else 23 if t > 3 else 26
    pts = []
    for k in range(8):
        a = k * 8192 + t * 1700
        rr = 1.0 if k % 2 == 0 else 0.5
        pts.append((a3_sin(a) * rr * f, -a3_sin(a + 16384) * rr * f * YS))
    return paint([(0, pts, pen)])


def draw_sight(f, i):
    """the original's sight (render.c hud): four arms 4..9 pixels out, scaled"""
    img = Image.new("P", (15, 15), 0)
    d = ImageDraw.Draw(img)
    for a, b in ((0, 4), (10, 14)):
        d.line([(a, 7), (b, 7)], fill=15)
        d.line([(7, a), (7, b)], fill=15)
    return img, 7, 7


# views: (name, how many frames, drawer(f, frame) -> image; unit size in world units)
def views(ms):
    return [
        ("TURRET", 1, lambda f, i: draw_model(ms["turret"], f, math.pi)),
        ("DRONE", 1, lambda f, i: draw_model(ms["drone"], f, math.pi)),
        ("POD", 1, lambda f, i: draw_squares(f, [(12, 17), (6, 1)])),
        ("BOLT", 1, lambda f, i: draw_squares(f, [(9, 19), (5, 18)])),
        ("BURST", 10, lambda f, i: draw_star(f, i + 1)),          # frame t - 1, radius 1 unit
        ("CORE", 8, lambda f, i: draw_model(ms["core"], f, i * 2 * math.pi / 8)),
        ("NODE", 8, lambda f, i: draw_model(ms["node"], f, i * 2 * math.pi / 8, math.pi / 2)),
        ("SIGHT", 1, draw_sight),
    ]


# the sizes each view needs (unit: pixels per unit): bursts are drawn at
# radius 1 unit, so their sizes go to the pixels of a 120-unit burst near by
SIZES = dict(TURRET=range(NSIZES), DRONE=range(NSIZES), POD=range(NSIZES), BOLT=range(NSIZES),
             BURST=range(NSIZES), CORE=range(4, 12), NODE=range(4, 12), SIGHT=range(1))
BURST_F0 = 60.0                         # burst size 0: pixels per unit of radius


def to_patterns(img, cols, rows):
    px = img.load()
    out = bytearray()
    for r in range(rows):
        for c in range(cols):
            for y in range(16):
                for x in range(0, 16, 2):
                    def get(xx, yy):
                        return px[xx, yy] if xx < img.width and yy < img.height else 0
                    out.append(get(c * 16 + x, r * 16 + y) << 4 | get(c * 16 + x + 1, r * 16 + y))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--show")
    args = ap.parse_args()
    ms = models()
    data, tab, sheet, vinfo = bytearray(), [], [], []
    for name, nframes, draw in views(ms):
        ks = list(SIZES[name])
        vinfo.append((name, nframes, ks[0], len(ks), len(tab)))
        for fr in range(nframes):
            row = []
            for k in ks:
                f = (BURST_F0 if name == "BURST" else F0) * RATIO ** k
                scale = 1
                while True:
                    img, ax, ay = draw(f / scale, fr)
                    if (img.width <= 32 and img.height <= 32) or scale == 8:
                        break
                    scale *= 2
                img = img.crop((0, 0, min(img.width, 32), min(img.height, 32)))
                cols, rows = (img.width + 15) // 16, (img.height + 15) // 16
                first = len(data) // 128
                data += to_patterns(img, cols, rows)
                tab.append(((cols - 1) | (rows - 1) << 1 | {1: 0, 2: 1, 4: 2, 8: 3}[scale] << 2,
                            min(255, round(ax)), min(255, round(ay)), first))
                row.append(img)
            sheet.append(row)
    os.makedirs(GFX, exist_ok=True)
    nbanks = (len(data) + 16383) // 16384
    with open(os.path.join(SRC, "spr_data.asm"), "w") as fh:
        fh.write("; the sprites' patterns (tools/gen_sprites.py), straight into banks by the .nex\n\n")
        for b in range(nbanks):
            with open(os.path.join(GFX, f"spr_{b}.bin"), "wb") as p:
                p.write(data[b * 16384:(b + 1) * 16384])
            fh.write(f"SECTION BANK_{SPR_BANK + b}\nBINARY \"gfx/spr_{b}.bin\"\n")
    with open(os.path.join(SRC, "spr_tab.c"), "w") as fh:
        fh.write("/* made by tools/gen_sprites.py */\n#include \"spr_views.h\"\n\n")
        fh.write("const SprView spr_view[SV_COUNT] = {\n")
        for name, nframes, k0, nk, at in vinfo:
            fh.write(f"    {{ {nframes}, {k0}, {nk}, {at} }},   /* {name} */\n")
        fh.write("};\n\nconst SprSize spr_tab[] = {\n")
        for shape, ax, ay, first in tab:
            fh.write(f"    {{ {shape}, {ax}, {ay}, {first} }},\n")
        fh.write("};\n")
    with open(os.path.join(SRC, "spr_views.h"), "w") as fh:
        fh.write("/* made by tools/gen_sprites.py */\n#ifndef SPR_VIEWS_H\n#define SPR_VIEWS_H\n#include <stdint.h>\n\n")
        fh.write("enum { " + ", ".join(f"SV_{v[0]}" for v in vinfo) + ", SV_COUNT };\n")
        fh.write(f"#define SPR_NSIZES {NSIZES}\n#define SPR_BANK {SPR_BANK}\n")
        fh.write(f"#define SPR_F0_X1000 {int(F0 * 1000)}      /* pixels per unit of size 0, x 1000 */\n")
        fh.write(f"#define SPR_BURST_F0_X1000 {int(BURST_F0 * 1000)}\n")
        fh.write("/* a view: frames, its first size (of the ladder), how many sizes, where in spr_tab;\n"
                 "   a size: cols - 1 (bit 0), rows - 1 (bit 1) of 16 x 16 patterns, scale (bits 2-3:\n"
                 "   x1..x8); the centre in the unscaled image; the first pattern (128 bytes each) */\n")
        fh.write("typedef struct { uint8_t frames, k0, nk; uint16_t at; } SprView;\n")
        fh.write("typedef struct { uint8_t shape, ax, ay; uint16_t first; } SprSize;\n"
                 "#define SPR_COLS(s) (((s)->shape & 1) + 1)\n#define SPR_ROWS(s) ((((s)->shape >> 1) & 1) + 1)\n"
                 "#define SPR_SCALE(s) ((s)->shape >> 2)\n")
        fh.write("extern const SprView spr_view[SV_COUNT];\nextern const SprSize spr_tab[];\n")
        fh.write("extern const uint16_t spr_pal12[16];\n#endif\n")
    with open(os.path.join(SRC, "spr_tab.c"), "a") as fh:
        fh.write("\nconst uint16_t spr_pal12[16] = { 0x000, " + ", ".join(f"0x{PAL12[k]:03X}" for k in range(1, 16)) + " };\n")
    print(f"{len(vinfo)} views: {len(data) // 128} patterns, {len(data)} bytes, banks {SPR_BANK}..{SPR_BANK + nbanks - 1}")
    if args.show:
        W = 34 * max(len(r) for r in sheet)
        out = Image.new("P", (W, 34 * len(sheet)), 0)
        pal = [0, 0, 0]
        for k in range(1, 16):
            c = PAL12[k]
            pal += [((c >> 8) & 15) * 17, ((c >> 4) & 15) * 17, (c & 15) * 17]
        out.putpalette(pal + [0] * (768 - len(pal)))
        for j, row in enumerate(sheet):
            for k, img in enumerate(row):
                out.paste(img, (k * 34, j * 34))
        out.resize((out.width * 2, out.height * 2), Image.NEAREST).save(args.show)


if __name__ == "__main__":
    main()
