#!/usr/bin/env python3
"""Warden Keep's pictures for the Spectrum Next, from the original's
render.cpp: its isometric projection (sx = (wx - wy) * 16 + 160,
sy = (wx + wy) * 8 - wz * 16 + 96), its cubes (top diamond, left and right
faces, outline), the room's floor, door and two back walls, the player
(a two-cube block with a notch on top for its facing), barrels and jewels.

port: the original draws in 8 flat colours; here the same shapes are
shaded in ramps of the Next's colours (stone floor with mortar, faces
lit from the left, wood barrels with iron bands, a cut jewel), and the
diamonds are diamonds (the original's fill_top_diamond grows the wrong
way: an hourglass; ORIGINAL_BUGS.md).

  gfx/room_<k>.bin   the room (floor, door, walls, HUD bar), 320 x 256
                     Layer 2, column-major, banks ROOM_BANK..+4
  gfx/spr.bin        sprite patterns (bank SPR_BANK): the barrel (2 x 2),
                     the player facing N E S W (2 x 3 each), the jewel
  src/gfx_data.c/.h  the palettes, the pattern slots, the banks
  build/preview.png  the room with the objects where the game starts them

    python3 tools/gen_gfx.py
"""
import os
import random
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(PROJ, "..", "..", "sdk", "tools"))
from nextpic import TRANSPARENT, group_patterns, l2_bytes, rgb9_from_rgb, write_banks  # noqa: E402

ROOM_BANK, SPR_BANK = 30, 35
W, H = 320, 256
TW, TH, CUBE = 32, 16, 16                # ISO_TILE_W, ISO_TILE_H, ISO_CUBE_H
ORG_X, ORG_Y = 160, 96
WORLD = 8
PLAY_H = 240


def iso(wx, wy, wz):
    return (wx - wy) * (TW // 2) + ORG_X, (wx + wy) * (TH // 2) - wz * CUBE + ORG_Y


# ---- palettes: ramps of RGB, index 0 = black ----
class Pal:
    def __init__(self):
        self.cols = [(0, 0, 0)]
        self.ramps = {}

    def ramp(self, name, dark, light, n):
        idx = []
        for k in range(n):
            t = k / max(1, n - 1)
            c = tuple(int(round(d + (l - d) * t)) for d, l in zip(dark, light))
            self.cols.append(c)
            idx.append(len(self.cols) - 1)
        self.ramps[name] = idx
        return idx

    def nine(self):
        out = [rgb9_from_rgb(*c) for c in self.cols]
        return out + [0] * (256 - len(out))


# the original's 8 pens (render.cpp PALETTE) are the middles of the ramps
L2 = Pal()
L2.ramp("floor", (40, 34, 10), (150, 128, 40), 8)          # pen 1 (128,112,32)
L2.ramp("mortar", (20, 16, 4), (56, 48, 16), 3)            # pen 4 (56,48,16)
L2.ramp("door", (24, 20, 90), (110, 100, 220), 6)          # pen 5 (64,56,168)
L2.ramp("wtop", (180, 150, 30), (255, 230, 90), 6)         # pen 2 (208,176,40)
L2.ramp("wleft", (90, 76, 20), (160, 140, 44), 6)          # pen 1
L2.ramp("wright", (150, 130, 40), (250, 230, 90), 6)       # pen 3 (248,232,80)
L2.ramp("hud", (40, 34, 10), (70, 60, 20), 2)              # the HUD bar, pen 4

SP = Pal()
SP.ramp("btop", (120, 80, 30), (220, 170, 90), 5)           # barrel: the original's pens 3 2 1 (golds) in wood
SP.ramp("bleft", (90, 56, 20), (170, 120, 50), 5)
SP.ramp("bright", (60, 36, 12), (130, 86, 34), 5)
SP.ramp("iron", (30, 30, 34), (110, 110, 120), 3)
SP.ramp("ptop", (170, 170, 170), (255, 255, 255), 4)       # player: pens 6 (white top), 7 (red left), 3 (gold right)
SP.ramp("pleft", (140, 30, 20), (250, 90, 70), 5)
SP.ramp("pright", (170, 150, 40), (255, 240, 100), 5)
SP.ramp("outline", (20, 16, 4), (56, 48, 16), 2)
SP.ramp("gem", (20, 90, 120), (140, 240, 255), 5)          # jewel: pens 3 + 6 (gold, white) as a cut gem
SP.cols.append((255, 255, 255))
SP.ramps["glint"] = [len(SP.cols) - 1]


def shade(ramp, t):
    t = min(0.999, max(0.0, t))
    return ramp[int(t * len(ramp))]


# ---- drawing (render.cpp's shapes on an index canvas; -1 = transparent) ----
def diamond_rows(cx, cy):
    """the top diamond's rows: (y, x0, x1) inclusive; port: widest at the middle"""
    out = []
    for i in range(TH // 2):
        w = ((TH // 2 - i) * (TW // 2)) // (TH // 2)
        out.append((cy - i, cx - w, cx + w - 1))
        if i:
            out.append((cy + i, cx - w, cx + w - 1))
    return out


def put(cv, x, y, c):
    if 0 <= y < cv.shape[0] and 0 <= x < cv.shape[1]:
        cv[y, x] = c


def line(cv, x0, y0, x1, y1, c):
    n = max(abs(x1 - x0), abs(y1 - y0), 1)
    for k in range(n + 1):
        put(cv, x0 + (x1 - x0) * k // n, y0 + (y1 - y0) * k // n, c)


def cube(cv, cx, cy, face_h, top, left, right, outline, tex=None):
    """render.cpp draw_stacked_cube: faces, the top, the outline; (cx, cy) the top's middle"""
    hw, hh = TW // 2, TH // 2
    for col in range(hw):                                  # left face, lit from the left
        x, y0 = cx - hw + col, cy + (col * hh) // hw
        for y in range(y0, y0 + face_h):
            t = 0.75 - 0.35 * (y - y0) / max(1, face_h) - 0.15 * col / hw
            put(cv, x, y, tex("l", col, y - y0) if tex else shade(left, t))
    for col in range(hw):                                  # right face, in shadow
        x, y0 = cx + col, cy + hh - (col * hh) // hw
        for y in range(y0, y0 + face_h):
            t = 0.65 - 0.35 * (y - y0) / max(1, face_h) - 0.2 * col / hw
            put(cv, x, y, tex("r", col, y - y0) if tex else shade(right, t))
    for y, x0, x1 in diamond_rows(cx, cy):
        for x in range(x0, x1 + 1):
            t = 0.85 - 0.25 * (x - x0) / max(1, x1 - x0) + 0.1 * (cy - y) / hh
            put(cv, x, y, tex("t", x - cx, y - cy) if tex else shade(top, t))
    o = outline
    line(cv, cx - hw, cy, cx, cy - hh, o)
    line(cv, cx, cy - hh, cx + hw - 1, cy - 1, o)
    line(cv, cx - hw, cy, cx - hw, cy + face_h, o)
    line(cv, cx, cy + hh, cx, cy + hh + face_h - 1, o)
    line(cv, cx + hw - 1, cy, cx + hw - 1, cy + face_h, o)
    line(cv, cx - hw, cy + face_h, cx, cy + hh + face_h, o)
    line(cv, cx, cy + hh + face_h, cx + hw - 1, cy + face_h, o)


def room():
    cv = np.zeros((H, W), np.int32)
    rnd = random.Random(7)
    floor, mortar = L2.ramps["floor"], L2.ramps["mortar"]
    # the floor (render.cpp draw_floor_tile): a diamond a cell, mortar edges, the door at (7, 7)
    for wy in range(WORLD):
        for wx in range(WORLD):
            cx, cy = iso(wx, wy, 0)
            ramp = L2.ramps["door"] if (wx, wy) == (WORLD - 1, WORLD - 1) else floor
            base = rnd.uniform(0.35, 0.65)
            for y, x0, x1 in diamond_rows(cx, cy):
                for x in range(x0, x1 + 1):
                    n = rnd.uniform(-0.12, 0.12)
                    put(cv, x, y, shade(ramp, base + n + 0.15 * (cy - y) / (TH // 2)))
            hw, hh = TW // 2, TH // 2
            m = mortar[1]
            line(cv, cx - hw, cy, cx, cy - hh, m)
            line(cv, cx, cy - hh, cx + hw - 1, cy - 1, m)
            line(cv, cx + hw - 1, cy, cx, cy + hh, mortar[0])
            line(cv, cx, cy + hh, cx - hw, cy, mortar[0])
    # the back walls (game.cpp load_room: wy = 0 and wx = 0, three cubes high), in the original's draw order
    walls = [(i, 0) for i in range(WORLD)] + [(0, i) for i in range(1, WORLD)]
    walls.sort(key=lambda p: p[0] + p[1])
    for wx, wy in walls:
        cx, cy = iso(wx, wy, 3)
        cube(cv, cx, cy, 3 * CUBE, L2.ramps["wtop"], L2.ramps["wleft"], L2.ramps["wright"], mortar[0])
        for k in (1, 2):                                   # courses of stone on the faces
            y = cy + k * CUBE
            line(cv, cx - TW // 2 + 1, y, cx - 1, y + TH // 2 - 1, L2.ramps["wleft"][0])
            line(cv, cx + 1, y + TH // 2 - 1, cx + TW // 2 - 2, y, L2.ramps["wright"][0])
    # the HUD bar (render.cpp draw_hud: pen 4 under the play area)
    cv[PLAY_H:, :] = L2.ramps["hud"][0]
    cv[PLAY_H, :] = L2.ramps["hud"][1]
    return cv


def sprite(face_h, draw):
    """a cube's sprite canvas: 32 wide, its top's middle at (16, 8)"""
    cv = np.full((TH + face_h, TW), -1, np.int32)
    draw(cv, 16, 8)
    return cv


def barrel_tex(kind, a, b):
    """port: wood staves and two iron bands on the original's barrel cube"""
    if kind == "t":
        r = (a * a + 4 * b * b) ** 0.5
        if 10 < r < 12.5:
            return SP.ramps["iron"][1]
        return shade(SP.ramps["btop"], 0.55 + 0.25 * ((int(r) // 3) % 2))
    ramp = SP.ramps["bleft"] if kind == "l" else SP.ramps["bright"]
    if b in (3, 4, 11, 12):
        return SP.ramps["iron"][2 if kind == "l" else 0]
    t = 0.7 - 0.3 * b / CUBE - (0.25 if a % 4 == 0 else 0.0) - (0.1 if kind == "r" else 0)
    return shade(ramp, t)


def barrel():
    return sprite(CUBE, lambda cv, x, y: cube(cv, x, y, CUBE, None, None, None, SP.ramps["outline"][0], barrel_tex))


def player(facing):
    """render.cpp draw_player: two cubes high, top 6 left 7 right 3, a black notch for its facing"""
    def draw(cv, cx, cy):
        cube(cv, cx, cy, 2 * CUBE, SP.ramps["ptop"], SP.ramps["pleft"], SP.ramps["pright"], SP.ramps["outline"][0])
        x0, y0, x1, y1 = [(-3, -6, 2, -4), (3, -2, 7, 1), (-3, 3, 2, 5), (-7, -2, -3, 1)][facing]
        for y in range(cy + y0, cy + y1 + 1):
            for x in range(cx + x0, cx + x1 + 1):
                put(cv, x, y, 0)
    return sprite(2 * CUBE, draw)


def jewel():
    """render.cpp draw_jewel: a small gem at the floor's middle (here 16 x 16, its point at (8, 13))"""
    cv = np.full((16, 16), -1, np.int32)
    gem = SP.ramps["gem"]
    rows = [(5, 2, 13), (6, 1, 14), (7, 1, 14), (8, 2, 13), (9, 3, 12), (10, 4, 11), (11, 5, 10), (12, 6, 9), (13, 7, 8)]
    for y, x0, x1 in rows:
        for x in range(x0, x1 + 1):
            t = 0.9 - 0.08 * (y - 5) - 0.04 * abs(x - 7.5) if y <= 7 else 0.55 - 0.05 * (y - 8) + (0.15 if x < 8 else 0)
            cv[y, x] = shade(gem, t)
    for x in range(2, 14):
        cv[7, x] = gem[-1] if x % 3 else gem[1]
    cv[5, 5] = cv[6, 4] = SP.ramps["glint"][0]
    return cv


def to_idx(cv):
    return np.where(cv < 0, TRANSPARENT, cv).astype(np.uint8)


def preview(cv_room, sprites):
    lut = np.array([c for c in L2.cols] + [(0, 0, 0)] * (256 - len(L2.cols)), np.uint8)
    img = lut[np.clip(cv_room, 0, 255)].copy()
    slut = np.array([c for c in SP.cols] + [(0, 0, 0)] * (256 - len(SP.cols)), np.uint8)
    for cv, (x, y) in sprites:
        h, w = cv.shape
        for yy in range(h):
            for xx in range(w):
                if cv[yy, xx] >= 0 and 0 <= y + yy < H and 0 <= x + xx < W:
                    img[y + yy, x + xx] = slut[cv[yy, xx]]
    return Image.fromarray(img, "RGB")


def main():
    assert len(L2.cols) < 0xE3 and len(SP.cols) < 0xE3
    rm = room()
    os.makedirs(os.path.join(PROJ, "build"), exist_ok=True)
    write_banks(l2_bytes(Image.fromarray(rm.astype(np.uint8), "P"), 320), ROOM_BANK,
                os.path.join("gfx", "room"), asm_file=False)
    # the sprite patterns
    pats = bytearray()
    slots = {}
    b = barrel()
    data, c, r = group_patterns(to_idx(b))
    slots["barrel"] = (len(pats) // 256, c, r)
    pats += data
    pl = [player(f) for f in range(4)]
    for f in range(4):
        data, c, r = group_patterns(to_idx(pl[f]))
        slots[f"player{f}"] = (len(pats) // 256, c, r)
        pats += data
    j = jewel()
    data, c, r = group_patterns(to_idx(j))
    slots["jewel"] = (len(pats) // 256, c, r)
    pats += data
    assert len(pats) <= 16384
    open(os.path.join(PROJ, "gfx", "spr.bin"), "wb").write(bytes(pats))
    with open(os.path.join(PROJ, "src", "gfx_data.asm"), "w") as f:
        f.write("; made by tools/gen_gfx.py: the room picture and the sprite patterns, straight into banks by the .nex\n")
        for k in range(5):
            f.write(f'SECTION BANK_{ROOM_BANK + k}\nBINARY "gfx/room_{k}.bin"\n')
        f.write(f'SECTION BANK_{SPR_BANK}\nBINARY "gfx/spr.bin"\n')
    l2p, spp = L2.nine(), SP.nine()
    with open(os.path.join(PROJ, "src", "gfx_data.c"), "w") as f:
        f.write('/* made by tools/gen_gfx.py - do not edit */\n#include "gfx_data.h"\n\n')
        f.write(f"const uint16_t room_pal[{len(L2.cols)}] = {{ " + ", ".join(f"0x{c:03X}" for c in l2p[:len(L2.cols)]) + " };\n")
        f.write(f"const uint16_t spr_pal[{len(SP.cols)}] = {{ " + ", ".join(f"0x{c:03X}" for c in spp[:len(SP.cols)]) + " };\n")
    with open(os.path.join(PROJ, "src", "gfx_data.h"), "w") as f:
        f.write("/* made by tools/gen_gfx.py - do not edit */\n#ifndef GFX_DATA_H\n#define GFX_DATA_H\n#include <stdint.h>\n")
        f.write(f"#define ROOM_BANK {ROOM_BANK}      /* the room: 320 x 256 Layer 2, 5 banks */\n")
        f.write(f"#define SPR_BANK  {SPR_BANK}      /* sprite patterns */\n#define SPR_PATTERNS {len(pats) // 256}\n")
        for name, (slot, c, r) in slots.items():
            f.write(f"#define PAT_{name.upper()} {slot}   /* {c} x {r} */\n")
        f.write(f"#define ROOM_COLOURS {len(L2.cols)}\n#define SPR_COLOURS {len(SP.cols)}\n")
        f.write("extern const uint16_t room_pal[ROOM_COLOURS];\nextern const uint16_t spr_pal[SPR_COLOURS];\n#endif\n")
    # a preview: the objects where load_room puts them, in the original's draw order
    objs = [("b", 2, 2), ("b", 5, 2), ("b", 3, 5), ("b", 5, 5), ("j", 6, 3), ("j", 2, 6), ("j", 6, 6)]
    objs.sort(key=lambda o: o[1] + o[2])
    sp = []
    for k, wx, wy in objs:
        if k == "b":
            cx, cy = iso(wx, wy, 1)
            sp.append((b, (cx - 16, cy - 8)))
        else:
            cx, cy = iso(wx, wy, 0)
            sp.append((j, (cx - 8, cy - 13)))
    cx, cy = iso(3, 6, 2)
    sp.append((pl[0], (cx - 16, cy - 8)))
    preview(rm, sp).save(os.path.join(PROJ, "build", "preview.png"))
    print(f"room {len(L2.cols)} colours, sprites {len(SP.cols)} colours, {len(pats) // 256} patterns: {slots}")


if __name__ == "__main__":
    main()
