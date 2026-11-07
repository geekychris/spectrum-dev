#!/usr/bin/env python3
"""ScAIry Fun's sprites: the original's polygon models (models.c: boxes and
pyramids, flat shaded by its fixed sun) drawn the way the game projects the
road (pseudo-3D: x * FOV / z, y * FOV / z from the chase camera's eye, CAM_UP
units over the road), at a ladder of sizes, as 4-bit 16 x 16 sprite patterns.

Each sprite colour is a pen of a 16-colour layout (index 0 transparent):
  1, 2 the body (lit, shaded; its colour comes from the palette offset),
  3 glass, 4 shadow, 5, 6 white, 7, 8 red, 9 yellow, 10 orange, 11, 12 brown,
  13 rock, 14 dirt, 15 the ambulance's light (red or blue by palette offset).

Output (gfx/):
  spr_data.bin    the patterns, 128 bytes each, from bank SPR_BANK onwards
  ../src/spr_data.asm  BANK_n sections that put them into the .nex
  spr_tab.c       for each view and size: cols, rows, hardware scale, the
                  anchor (where the model's foot is), the first pattern
  spr_views.h     view numbers, size count

  ./tools/gen_sprites.py [--show out.png]
"""
import argparse
import math
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
GFX = os.path.join(HERE, "..", "gfx")
SRC = os.path.join(HERE, "..", "src")

FOV = 144                       # render.h
CAM_UP = 92                     # render.c: the chase camera over the road
SPR_BANK = 40                   # 16K banks 40.. (8K pages 80..)
NSIZES = 16
F0, RATIO = 2.4, 0.8            # pixels per unit of the biggest size, the step

# ---------------------------------------------------------------- models.c
PEN = dict(BLACK=0, WHITE=1, PANEL=2, GREY=3, RED=16, YELLOW=18, ORANGE=19, BLUE=22,
           CARW=24, KHAKI=26, TREE=27, GLASS=28, BROWN=29, SHADOW=31)
COL_BODY, COL_FLASH = 200, 210
MAT = dict(BODY=(COL_BODY, COL_BODY + 1), GLASS=(28, 28), DARK=(31, 31), WHITE=(24, 25),
           BROWN=(30, 29), TREE=(27, 27), ORANGE=(19, 19), CONCRETE=(3, 2), RED=(16, 17),
           FLASH=(COL_FLASH, COL_FLASH), YELLOW=(18, 19), ROCK=(15, 13), SKIN=(30, 30))
# original pen -> sprite colour index
SPEN = {COL_BODY: 1, COL_BODY + 1: 2, 28: 3, 31: 4, 24: 5, 25: 6, 16: 7, 17: 8, 18: 9, 19: 10,
        29: 11, 30: 12, 15: 13, 13: 14, COL_FLASH: 15, 3: 5, 2: 6, 27: 4}


class Model:
    def __init__(self):
        self.v, self.f = [], []

    def vtx(self, x, y, z):
        self.v.append((x, y, z))
        return len(self.v) - 1

    def face(self, mat, idx, part):
        a, b, c = (self.v[i] for i in idx[:3])
        u = [b[k] - a[k] for k in range(3)]
        w = [c[k] - a[k] for k in range(3)]
        n = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        out = sum(n[k] * (a[k] - part[k]) for k in range(3))
        if out < 0:
            idx = list(reversed(idx))
            n = tuple(-x for x in n)
        light = -n[0] + 2 * n[1] + n[2]
        mag = sum(abs(x) for x in n)
        col = MAT[mat][0 if (mag and light * 4 > mag * 3) else 1]
        self.f.append((list(idx), col, n))

    def box(self, mat, x0, x1, y0, y1, z0, z1):
        b = len(self.v)
        part = ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
        for (x, y, z) in [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                          (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]:
            self.vtx(x, y, z)
        for q in [(0, 3, 2, 1), (5, 6, 7, 4), (4, 7, 3, 0), (1, 2, 6, 5), (3, 7, 6, 2)]:
            self.face(mat, [b + i for i in q], part)

    def pyramid(self, mat, x0, x1, y0, z0, z1, apex):
        b = len(self.v)
        part = ((x0 + x1) / 2, y0 + (apex - y0) / 4, (z0 + z1) / 2)
        for (x, z) in [(x0, z0), (x1, z0), (x1, z1), (x0, z1)]:
            self.vtx(x, y0, z)
        t = self.vtx((x0 + x1) / 2, apex, (z0 + z1) / 2)
        for k in range(4):
            self.face(mat, [b + k, b + (k + 1) % 4, t], part)

    def shadow(self, x0, x1, z0, z1):
        b = len(self.v)
        for (x, z) in [(x0, z0), (x1, z0), (x1, z1), (x0, z1)]:
            self.vtx(x, 1, z)
        self.face("DARK", [b, b + 1, b + 2, b + 3], (0, -10, 0))
        self.f[-1] = (self.f[-1][0], self.f[-1][1], "shadow")


def models():
    m = {}
    c = m["car"] = Model()
    c.shadow(-16, 16, -38, 38)
    c.box("BODY", -14, 14, 3, 11, -36, 36)
    c.box("GLASS", -12, 12, 11, 19, -15, 12)
    c.box("BODY", -12, 12, 19, 21, -13, 10)
    t = m["truck"] = Model()
    t.shadow(-21, 21, -92, 92)
    t.box("WHITE", -20, 20, 5, 58, -90, 50)
    t.box("BODY", -19, 19, 3, 44, 54, 90)
    t.box("GLASS", -17, 17, 30, 42, 88, 91)
    t = m["tractor"] = Model()
    t.box("RED", -9, 9, 8, 22, -4, 30)
    t.box("GLASS", -10, 10, 22, 42, -26, -4)
    t.box("DARK", -17, -10, 0, 26, -28, -6)
    t.box("DARK", 10, 17, 0, 26, -28, -6)
    t = m["cyclist"] = Model()
    t.box("DARK", -1, 1, 1, 13, -14, 14)
    t.box("YELLOW", -4, 4, 13, 26, -5, 4)
    t.box("SKIN", -3, 3, 26, 32, -3, 3)
    t = m["deer"] = Model()
    t.box("BROWN", -3, 3, 0, 14, -11, 11)
    t.box("BROWN", -5, 5, 14, 24, -14, 14)
    t.box("BROWN", -3, 3, 22, 36, 10, 18)
    t = m["goat"] = Model()
    t.box("WHITE", -2, 2, 0, 9, -7, 7)
    t.box("WHITE", -4, 4, 9, 17, -9, 9)
    t.box("WHITE", -2, 2, 15, 23, 7, 12)
    t = m["ped"] = Model()
    t.box("DARK", -3, 3, 0, 14, -2, 2)
    t.box("BODY", -4, 4, 14, 26, -3, 3)
    t.box("SKIN", -2, 2, 26, 31, -2, 2)
    t = m["cone"] = Model()
    t.pyramid("ORANGE", -5, 5, 0, -5, 5, 13)
    t = m["tyre"] = Model()
    t.box("DARK", -10, 10, 0, 7, -9, 9)
    t = m["rock"] = Model()
    t.pyramid("ROCK", -11, 11, 0, -9, 9, 13)
    t = m["ambulance"] = Model()
    t.shadow(-18, 18, -52, 52)
    t.box("WHITE", -17, 17, 3, 44, -50, 28)
    t.box("WHITE", -16, 16, 3, 28, 28, 50)
    t.box("GLASS", -14, 14, 18, 27, 49, 51)
    t.box("RED", -18, 18, 20, 26, -51, 28)
    t.box("FLASH", -10, 10, 44, 49, 20, 27)
    return m


# views: (name, model, yaw in degrees: 0 = seen from behind, 180 from the front,
#         -90 its front to the left)
VIEWS = [
    ("CAR", "car", 0), ("CAR_FRONT", "car", 180), ("TRUCK", "truck", 0), ("TRUCK_FRONT", "truck", 180),
    ("TRACTOR", "tractor", 0), ("CYCLIST", "cyclist", 0), ("DEER", "deer", 0), ("DEER_SIDE", "deer", -90),
    ("CONE", "cone", 0), ("TYRE", "tyre", 0), ("ROCK", "rock", 0), ("AMBULANCE", "ambulance", 0),
    ("GOAT", "goat", 0), ("GOAT_SIDE", "goat", -90), ("PED", "ped", 0), ("PED_SIDE", "ped", -90),
    ("PLAYER", "car", 0), ("PLAYER_L", "car", -10),
]


def render(model, yaw_deg, f):
    """the model at pixels-per-unit f (its foot at depth FOV / f) -> (RGBA-free
    index image, anchor x, anchor y)"""
    D = FOV / f
    a = math.radians(yaw_deg)
    co, si = math.cos(a), math.sin(a)
    pts = []
    for (x, y, z) in model.v:
        rx, rz = x * co + z * si, z * co - x * si
        zz = D + rz
        zz = max(zz, 8)
        pts.append((rx * FOV / zz, -(y - CAM_UP) * FOV / zz, zz, rx, y, rz))
    base_y = CAM_UP * FOV / D
    # visible faces, far first (shadows first of all)
    faces = []
    for idx, col, n in model.f:
        p = [pts[i] for i in idx]
        if n == "shadow":
            depth = 1e9
        else:
            # rotated normal; visible if it faces the eye at (0, CAM_UP, -D) relative
            nx, ny, nz = n
            rnx, rnz = nx * co + nz * si, nz * co - nx * si
            vx, vy, vz = 0 - p[0][3], CAM_UP - p[0][4], -D - p[0][5]
            if rnx * vx + ny * vy + rnz * vz <= 0:
                continue
            depth = sum(q[2] for q in p)
        faces.append((depth, [(q[0], q[1]) for q in p], col))
    faces.sort(key=lambda t: -t[0])
    xs = [x for _, poly, _ in faces for x, _ in poly] or [0]
    ys = [y for _, poly, _ in faces for _, y in poly] or [0]
    x0, x1 = math.floor(min(xs)), math.ceil(max(xs))
    y0, y1 = math.floor(min(ys)), math.ceil(max(ys))
    w, h = max(1, x1 - x0 + 1), max(1, y1 - y0 + 1)
    img = Image.new("P", (w, h), 0)
    d = ImageDraw.Draw(img)
    for _, poly, col in faces:
        d.polygon([(x - x0, y - y0) for x, y in poly], fill=SPEN.get(col, 6))
    return img, -x0, base_y - y0


def to_patterns(img, cols, rows):
    """4-bit 16 x 16 patterns, TL to BR row by row: 128 bytes each"""
    px = img.load()
    out = bytearray()
    for r in range(rows):
        for c in range(cols):
            for y in range(16):
                for x in range(0, 16, 2):
                    def get(xx, yy):
                        return px[xx, yy] if xx < img.width and yy < img.height else 0
                    hi, lo = get(c * 16 + x, r * 16 + y), get(c * 16 + x + 1, r * 16 + y)
                    out.append(hi << 4 | lo)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--show", help="a sheet of every view and size, for looking at")
    args = ap.parse_args()
    ms = models()
    data, tab, sheet = bytearray(), [], []
    for name, mname, yaw in VIEWS:
        row = []
        for k in range(NSIZES):
            f = F0 * RATIO ** k
            scale = 1
            while True:
                img, ax, ay = render(ms[mname], yaw, f / scale)
                if (img.width <= 32 and img.height <= 32) or scale == 8:
                    break
                scale *= 2
            img = img.crop((0, 0, min(img.width, 32), min(img.height, 32)))
            cols, rows = (img.width + 15) // 16, (img.height + 15) // 16
            first = len(data) // 128
            data += to_patterns(img, cols, rows)
            tab.append((cols, rows, {1: 0, 2: 1, 4: 2, 8: 3}[scale], round(ax), round(ay), first))
            row.append(img)
        sheet.append(row)
    os.makedirs(GFX, exist_ok=True)
    with open(os.path.join(GFX, "spr_data.bin"), "wb") as fh:
        fh.write(data)
    nbanks = (len(data) + 16383) // 16384
    with open(os.path.join(SRC, "spr_data.asm"), "w") as fh:
        fh.write("; the sprites' patterns (tools/gen_sprites.py), straight into banks by the .nex\n\n")
        for b in range(nbanks):
            part = os.path.join(GFX, f"spr_{b}.bin")
            with open(part, "wb") as p:
                p.write(data[b * 16384:(b + 1) * 16384])
            fh.write(f"SECTION BANK_{SPR_BANK + b}\nBINARY \"gfx/spr_{b}.bin\"\n")
    with open(os.path.join(SRC, "spr_tab.c"), "w") as fh:
        fh.write("/* made by tools/gen_sprites.py: each view at NSIZES sizes */\n")
        fh.write('#include "spr_views.h"\n\nconst SprSize spr_tab[SV_COUNT * SPR_NSIZES] = {\n')
        for i, (name, _, _) in enumerate(VIEWS):
            fh.write(f"    /* {name} */\n")
            for k in range(NSIZES):
                c, r, s, ax, ay, first = tab[i * NSIZES + k]
                fh.write(f"    {{ {(c - 1) | (r - 1) << 1 | s << 2}, {ax}, {ay}, {first} }},\n")
        fh.write("};\n")
    with open(os.path.join(SRC, "spr_views.h"), "w") as fh:
        fh.write("/* made by tools/gen_sprites.py */\n#ifndef SPR_VIEWS_H\n#define SPR_VIEWS_H\n#include <stdint.h>\n\n")
        fh.write("enum { " + ", ".join(f"SV_{n}" for n, _, _ in VIEWS) + ", SV_COUNT };\n")
        fh.write(f"#define SPR_NSIZES {NSIZES}\n#define SPR_BANK {SPR_BANK}\n")
        fh.write(f"#define SPR_F0_X1000 {int(F0 * 1000)}   /* pixels per unit of size 0, x 1000 */\n")
        fh.write("/* shape: cols - 1 (bit 0), rows - 1 (bit 1) of 16 x 16 patterns, scale (bits\n"
                 "   2-3: x1..x8); anchor: the foot's position in the unscaled image; the\n"
                 "   first pattern (128 bytes each) */\n")
        fh.write("typedef struct { uint8_t shape, ax, ay; uint16_t first; } SprSize;\n"
                 "#define SPR_COLS(s) (((s)->shape & 1) + 1)\n#define SPR_ROWS(s) ((((s)->shape >> 1) & 1) + 1)\n"
                 "#define SPR_SCALE(s) ((s)->shape >> 2)\n")
        fh.write("extern const SprSize spr_tab[SV_COUNT * SPR_NSIZES];\n#endif\n")
    print(f"{len(VIEWS)} views x {NSIZES} sizes: {len(data) // 128} patterns, {len(data)} bytes, banks {SPR_BANK}..{SPR_BANK + nbanks - 1}")
    if args.show:
        pal = [0, 0, 0, 40, 90, 200, 20, 50, 140, 30, 40, 60, 17, 17, 17, 238, 238, 238, 170, 170, 187,
               238, 34, 34, 136, 17, 17, 255, 238, 34, 255, 119, 34, 102, 68, 34, 187, 136, 85, 119, 102, 102,
               136, 119, 68, 255, 0, 255]
        W = sum(34 for _ in range(NSIZES))
        out = Image.new("P", (W, 34 * len(sheet)), 0)
        out.putpalette(pal + [0] * (768 - len(pal)))
        for j, row in enumerate(sheet):
            for k, img in enumerate(row):
                out.paste(img, (k * 34, j * 34))
        out = out.resize((out.width * 3, out.height * 3), Image.NEAREST)
        out.save(args.show)


if __name__ == "__main__":
    main()
