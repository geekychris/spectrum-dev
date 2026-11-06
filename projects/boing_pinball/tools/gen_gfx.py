#!/usr/bin/env python3
"""Boing Pinball's pictures for the Spectrum Next.

From a table's JSON (tools/export_table, the original pb_tables.c) this
draws, the way the original's render.c does (its palette, its wall3d /
cylinder / ramp / lamp / deco drawing, its oblique view: 0.55 : 0.44 : 0.33
pixels a mm across, up and in height), scaled so the whole table fits the
Next's 320 x 256 Layer 2:

  gfx/l2_<n>.bin   the screen, 320 x 256, column-major (Layer 2's
                         320 mode), palette indices
  gfx/spr.bin      sprite patterns: 8 Boing-ball frames, 8 frames of
                         the left flipper as 2 x 2 patterns (the right one
                         is the same, mirrored), the plunger
  src/gfx_<n>.c          the palettes and which palette entry each lamp,
                         drop target, bumper, sling and spinner owns (they
                         light up by changing their palette entry)

    python3 tools/gen_gfx.py build/host/table_0.json 0
"""
import json
import math
import os
import re
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

S = 0.49                                  # the original's view, scaled to fit 256 rows
KX, KY, KZ = 0.55 * S, 0.44 * S, 0.33 * S
OX, OY = 8, 241                           # screen column of x = 0, row of y = 0 at height 0
W, H = 320, 256

# render.c's pens and palettes (12-bit Amiga RGB)
(P_BLACK, P_FIELD1, P_FIELD2, P_ART1, P_ART2, P_ART3, P_METAL_D, P_METAL_L,
 P_CAB_D, P_CAB_L, P_RUBBER, P_RUBBER_D, P_RED, P_RED_D, P_FLASH, P_SHADOW,
 P_BLUE, P_BLUE_D, P_YEL, P_YEL_D, P_LRED, P_LRED_D, P_GREEN, P_MAG,
 P_ORANGE, P_DIM, P_DMD_BG, P_DMD_ON, P_DMD_MID, P_GLASS, P_WOOD, P_TEXT) = range(32)
COMMON = [0x000, 0x213, 0x324, 0x436, 0x547, 0xA86, 0x667, 0xCCD,
          0x413, 0x736, 0xFFE, 0x999, 0xE22, 0x811, 0xFF8, 0x101,
          0x5BF, 0x235, 0xFD4, 0x542, 0xF54, 0x522, 0x6F9, 0xF4F,
          0xFB2, 0x443, 0x210, 0xFA2, 0x741, 0x8CF, 0x631, 0xFFF]
THEME = [[0x213, 0x324, 0x436, 0x547, 0xA86, 0x413, 0x736, 0x102, 0x631],
         [0x024, 0x035, 0x047, 0x0A9, 0xEC8, 0x134, 0x368, 0x012, 0x234],
         [0x322, 0x433, 0x544, 0x765, 0xDA2, 0x333, 0x777, 0x111, 0x421]]
LAMP_ON = [P_BLUE, P_YEL, P_LRED, P_GREEN, P_MAG, P_ORANGE]
LAMP_OFF = [P_BLUE_D, P_YEL_D, P_LRED_D, P_DIM, P_DIM, P_DIM]
SK_WALL, SK_OUTER, SK_SLING, SK_GATE, SK_FLOOR, SK_DROP, SK_GUIDE = range(7)
SN_SPIN = 3

# palette entries that animate (src/gfx.h)
I_LAMP, I_DROP, I_BUMPER, I_SLING, I_SPIN = 32, 64, 72, 82, 84
I_PANEL = 96                              # the side panel's own colours
PANEL = {"bg": 0x112, "frame": 0xA86, "title": 0xFD4, "text": 0xCCD, "dim": 0x667, "dmd_frame": 0x333}


def font():
    """the 5x7 font of this project (src/font.c, the Amiga StakAttack.s), '!'..'Z'"""
    src = open(os.path.join(ROOT, "src", "font.c")).read()
    rows = re.findall(r"\{ (0x[0-9A-F]{2}(?:, 0x[0-9A-F]{2}){6}) \}", src)
    return [[int(v, 16) for v in r.split(", ")] for r in rows]


FONT = font()


def px(x):
    return OX + x * KX


def py(y, z=0):
    return OY - y * KY - z * KZ


class Canvas:
    def __init__(self, w, h, bg):
        self.im = Image.new("P", (w, h), bg)
        self.d = ImageDraw.Draw(self.im)

    def poly(self, pts, col):
        self.d.polygon([(round(x), round(y)) for x, y in pts], fill=col)

    def ellipse(self, cx, cy, rx, ry, col):
        rx, ry = max(rx, 0.5), max(ry, 0.5)
        self.d.ellipse([round(cx - rx), round(cy - ry), round(cx + rx), round(cy + ry)], fill=col)

    def line(self, x0, y0, x1, y1, col, w=1):
        self.d.line([(round(x0), round(y0)), (round(x1), round(y1))], fill=col, width=w)

    def rect(self, x0, y0, x1, y1, col):
        self.d.rectangle([round(x0), round(y0), round(x1), round(y1)], fill=col)

    def dither_poly(self, pts, col):
        mask = Image.new("1", self.im.size, 0)
        ImageDraw.Draw(mask).polygon([(round(x), round(y)) for x, y in pts], fill=1)
        mp, ip = mask.load(), self.im.load()
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        for y in range(max(0, int(min(ys)) - 1), min(self.im.height, int(max(ys)) + 2)):
            for x in range(max(0, int(min(xs)) - 1), min(self.im.width, int(max(xs)) + 2)):
                if mp[x, y] and (x + y) & 1:
                    ip[x, y] = col

    def text(self, x, y, s, col, scale=1):
        for ch in s:
            o = ord(ch) - 33
            if 0 <= o < len(FONT):
                for r, bits in enumerate(FONT[o]):
                    for c in range(5):
                        if bits & (0x80 >> c):
                            self.rect(x + c * scale, y + r * scale, x + c * scale + scale - 1, y + r * scale + scale - 1, col)
            x += 6 * scale


def text_w(s, scale=1):
    return len(s) * 6 * scale - scale


def wall3d(cv, ax, ay, bx, by, r, h, side, top):
    ex, ey = bx - ax, by - ay
    ln = math.hypot(ex, ey)
    nx, ny = (r, 0) if ln < 0.25 else (-ey / ln * r, ex / ln * r)
    c = [(ax + nx, ay + ny), (ax - nx, ay - ny), (bx - nx, by - ny), (bx + nx, by + ny)]
    for k in range(4):                    # the sides: each face from the floor to the top
        (x0, y0), (x1, y1) = c[k], c[(k + 1) % 4]
        cv.poly([(px(x0), py(y0)), (px(x1), py(y1)), (px(x1), py(y1, h)), (px(x0), py(y0, h))], side)
    cv.ellipse(px(ax), py(ay), r * KX, r * KY, side)
    cv.ellipse(px(bx), py(by), r * KX, r * KY, side)
    cv.rect(px(ax) - r * KX, py(ay, h), px(ax) + r * KX, py(ay), side)
    cv.rect(px(bx) - r * KX, py(by, h), px(bx) + r * KX, py(by), side)
    cv.poly([(px(x), py(y, h)) for x, y in c], top)
    cv.ellipse(px(ax), py(ay, h), r * KX, r * KY, top)
    cv.ellipse(px(bx), py(by, h), r * KX, r * KY, top)


def cylinder(cv, x, y, r, h, side, top):
    cx, y0, y1, rx, ry = px(x), py(y), py(y, h), r * KX, r * KY
    cv.ellipse(cx, y0, rx, ry, side)
    cv.rect(cx - rx, y1, cx + rx, y0, side)
    cv.ellipse(cx, y1, rx, ry, top)


def field_edges(y):
    if y > 780:
        d = y - 780
        w = math.sqrt(260 * 260 - d * d) if d < 260 else 0
        return 260 - w, 260 + w
    xl = 20 if y <= 380 else (20 - (y - 380) * 20 / 90 if y <= 470 else 0)
    return xl, 520


def ramp_curve(pts):
    out = []
    n = len(pts)
    for i in range(n - 1):
        for k in range(4):
            t = k / 4
            p0, p1, p2, p3 = pts[max(i - 1, 0)], pts[i], pts[i + 1], pts[min(i + 2, n - 1)]
            out.append([0.5 * (2 * p1[c] + (-p0[c] + p2[c]) * t + (2 * p0[c] - 5 * p1[c] + 4 * p2[c] - p3[c]) * t * t +
                               (-p0[c] + 3 * p1[c] - 3 * p2[c] + p3[c]) * t ** 3) for c in range(3)])
    out.append(list(pts[-1]))
    return out


def draw_table(d, theme):
    cv = Canvas(W, H, P_WOOD)
    # the playfield
    for row in range(H):
        ymm = (OY - row) / KY
        if ymm < -60 or ymm > 1040:
            continue
        xl, xr = field_edges(ymm)
        if xl < xr:
            cv.line(px(xl), row, px(xr), row, P_FIELD1)
            if 380 < ymm < 760 and row & 1:
                cv.line(px(xl + 30), row, px(xr - 30 if xr < 470 else 440), row, P_FIELD2)
    # the theme's art (render.c theme_art: its LCG, its positions)
    r = 12345 + theme
    def rnd():
        nonlocal r
        r = (r * 1103515245 + 12345) & 0xFFFFFFFF
        return (r >> 16) & 0x7FFF
    for i in range(160):
        x, y = rnd() % 500 + 10, rnd() % 1060 - 40
        xl, xr = field_edges(y)
        if x < xl + 6 or x > (xr - 6 if xr < 476 else 476):
            continue
        if theme == 0:
            cv.rect(px(x), py(y), px(x), py(y), P_ART3 if i & 3 else P_TEXT)
        elif theme == 1:
            if i & 1:
                cv.ellipse(px(x), py(y), 0.6, 0.6, P_ART2)
        elif not i & 3:
            cv.rect(px(x), py(y), px(x) + 1, py(y), P_ART2)
    for kind, col, x, y, a, b, text, dpx, dpy in d["deco"]:
        if kind == 0:
            cv.ellipse(px(x), py(y), a * KX, b * KY, col)
        elif kind == 1:
            for i in range(48):
                a0, a1 = i * 2 * math.pi / 48, (i + 1) * 2 * math.pi / 48
                cv.line(px(x) + a * KX * math.cos(a0), py(y) + b * KY * math.sin(a0),
                        px(x) + a * KX * math.cos(a1), py(y) + b * KY * math.sin(a1), col, 1)
        elif kind == 2:
            cv.poly([(px(dpx[i]), py(dpy[i])) for i in range(4)], col)
        elif kind == 4 and text:
            cv.text(round(px(239) - text_w(text) / 2) + 1, round(py(y)) - 3, text, P_ART2)
            cv.text(round(px(239) - text_w(text) / 2), round(py(y)) - 4, text, col)
    # the lamps, unlit: each its own palette entry
    lamps = []
    for i, (x, y, r_, col) in enumerate(d["lamp"]):
        if not r_:
            continue
        cv.ellipse(px(x), py(y), r_ * KX + 0.7, r_ * KY + 0.7, P_BLACK)
        cv.ellipse(px(x), py(y), max(r_ * KX, 1.0), max(r_ * KY, 0.8), I_LAMP + i)
        lamps.append((i, col))
    # the saucer
    sx, sy, sr = d["saucer"]
    cv.ellipse(px(sx), py(sy), (sr + 4) * KX, (sr + 4) * KY, P_METAL_L)
    cv.ellipse(px(sx), py(sy), sr * KX, sr * KY, P_BLACK)
    # the drop targets: the slot, and the target in its own palette entry
    # (standing: its colour; down: the slot's)
    for i, si in enumerate(d["drop_seg"]):
        ax, ay, bx, by = d["seg"][si][:4]
        cv.line(px(ax), py(ay), px(bx), py(by), P_BLACK, 2)
    # walls, posts and bumpers, far to near
    items = []
    for i, s in enumerate(d["seg"]):
        if s[6] in (SK_DROP, SK_GATE):
            continue
        items.append((30000 if s[6] == SK_OUTER else min(s[1], s[3]), 0, i))
    for i, c in enumerate(d["circ"]):
        items.append((c[1], 1, i))
    items.sort(key=lambda t: -t[0])
    for _, kind, i in items:
        if kind == 0:
            ax, ay, bx, by, r_, e, k, sid, owx, owy, bank, h, up = d["seg"][i]
            side = P_CAB_D if k == SK_OUTER else P_RUBBER_D if k == SK_SLING else P_METAL_D
            top = P_CAB_L if k == SK_OUTER else (I_SLING + (sid & 1)) if k == SK_SLING else P_METAL_L
            wall3d(cv, ax, ay, bx, by, r_, h, side, top)
        else:
            cx, cy, r_, e, bumper, cid, h = d["circ"][i]
            if bumper:
                cv.ellipse(px(cx), py(cy), (r_ + 3) * KX, (r_ + 3) * KY, P_RUBBER)
                cylinder(cv, cx, cy, r_ - 2, h - 8, P_METAL_D, P_RUBBER_D)
                cylinder(cv, cx, cy + 1, r_ * 0.9, h, P_RED_D, I_BUMPER + i)
                cv.ellipse(px(cx) - 1.5, py(cy, h) - 0.5, r_ * KX / 3, r_ * KY / 3, P_LRED)
            else:
                cylinder(cv, cx, cy, r_, h, P_RUBBER_D, P_RUBBER)
    # the drop targets, standing (their palette entry hides them when down)
    for i, si in enumerate(d["drop_seg"]):
        ax, ay, bx, by = d["seg"][si][:4]
        wall3d(cv, ax, ay, bx, by, 3, 26, I_DROP + i, I_DROP + i)
    # the gates: a wire on two posts
    for s in d["seg"]:
        if s[6] != SK_GATE:
            continue
        ax, ay, bx, by, h = s[0], s[1], s[2], s[3], s[11]
        cv.line(px(ax), py(ay, h), px(bx), py(by, h), P_METAL_L)
        cv.line(px(ax), py(ay), px(ax), py(ay, h), P_METAL_D)
        cv.line(px(bx), py(by), px(bx), py(by, h), P_METAL_D)
    # the spinners: brackets and a blade in its own palette entry
    for ax, ay, bx, by, kind, idx in d["sens"]:
        if kind != SN_SPIN:
            continue
        cv.line(px(ax) + 1, py(ay), px(ax) + 1, py(ay, 30), P_METAL_D, 1)
        cv.line(px(bx) - 1, py(by), px(bx) - 1, py(by, 30), P_METAL_D, 1)
        cv.rect(px(ax) + 2, py(ay, 30), px(bx) - 2, py(ay, 10), I_SPIN + idx)
        cv.line(px(ax) + 1, py(ay, 30), px(bx) - 1, py(ay, 30), P_METAL_L)
    # the apron over the drain
    cv.poly([(px(20), py(22, 20)), (px(474), py(22, 20)), (px(474), py(-60)), (px(20), py(-60))], P_CAB_D)
    cv.line(px(20), py(22, 20), px(474), py(22, 20), P_CAB_L, 1)
    t = "BOING PINBALL"
    cv.text(round(px(239) - text_w(t) / 2), round(py(-20, 20)), t, P_ART3)
    # the ramps, over everything
    for rp in d["ramp"]:
        pts = ramp_curve(rp["pts"])
        half, rail = 22, 14
        for i in range(4, len(pts) - 2, 6):
            if pts[i][2] > 12:
                cv.line(px(pts[i][0]), py(pts[i][1]), px(pts[i][0]), py(pts[i][1], pts[i][2]), P_METAL_D, 1)
        for i in range(len(pts) - 1):
            (x0, y0, z0), (x1, y1, z1) = pts[i], pts[i + 1]
            ln = math.hypot(x1 - x0, y1 - y0)
            if not ln:
                continue
            nx, ny = -(y1 - y0) * half / ln, (x1 - x0) * half / ln
            cv.dither_poly([(px(x0 + nx), py(y0 + ny, z0)), (px(x1 + nx), py(y1 + ny, z1)),
                            (px(x1 - nx), py(y1 - ny, z1)), (px(x0 - nx), py(y0 - ny, z0))], P_GLASS)
            for s in (-1, 1):
                cv.line(px(x0 + s * nx), py(y0 + s * ny, z0 + rail), px(x1 + s * nx), py(y1 + s * ny, z1 + rail), P_METAL_L, 1)
    return cv, lamps


def draw_panel(cv, d):
    """the right-hand side: the name, the dot-matrix display's frame, the keys"""
    x0 = 160
    cv.rect(x0, 0, W - 1, H - 1, I_PANEL + 0)
    cv.rect(x0 + 2, 2, W - 3, H - 3, I_PANEL + 1)
    cv.rect(x0 + 4, 4, W - 5, H - 5, I_PANEL + 0)
    cv.text(x0 + 80 - text_w("BOING", 3) // 2, 10, "BOING", I_PANEL + 2, 3)
    cv.text(x0 + 80 - text_w("PINBALL", 2) // 2, 36, "PINBALL", I_PANEL + 2, 2)
    # the dot-matrix display (src/main.c draws in it): DMD_X..+DMD_W, DMD_Y..+DMD_H
    cv.rect(DMD_X - 3, DMD_Y - 3, DMD_X + DMD_W + 2, DMD_Y + DMD_H + 2, I_PANEL + 5)
    cv.rect(DMD_X, DMD_Y, DMD_X + DMD_W - 1, DMD_Y + DMD_H - 1, P_DMD_BG)
    name = d["name"]
    cv.text(x0 + 80 - text_w(name) // 2, 120, name, I_PANEL + 3)
    keys = ["Z  M   FLIPPERS", "SPACE  PLUNGER", "Q      NUDGE", "", "JOYSTICK: LEFT RIGHT", "FIRE UP"]
    for i, k in enumerate(keys):
        cv.text(x0 + 12, 186 + i * 10, k, I_PANEL + 4)


DMD_X, DMD_Y, DMD_W, DMD_H = 168, 58, 144, 48


def rgb9(c12):
    """12-bit Amiga RGB -> the Next's 9-bit RGB333"""
    r, g, b = (c12 >> 8) & 15, (c12 >> 4) & 15, c12 & 15
    return (r >> 1) << 6 | (g >> 1) << 3 | (b >> 1)


def ball_frames(br=3.6):
    """render.c make_ball: the Boing ball, 8 frames of one checker square's turn"""
    frames = []
    c20, s20 = math.cos(math.radians(20)), math.sin(math.radians(20))
    for f in range(8):
        pat = [0xE3] * 256
        for j in range(16):
            for i in range(16):
                x, y = (i - 7.5) / br, (j - 7.5) / (br * 0.85)
                if x * x + y * y > 1:
                    continue
                nz = math.sqrt(max(0, 1 - x * x - y * y))
                xr, yr = x * c20 - y * s20, x * s20 + y * c20
                lon = math.atan2(xr, nz) / (2 * math.pi) * 8 + f / 8
                lat = math.atan2(yr, math.sqrt(max(0, 1 - yr * yr))) / (2 * math.pi) * 8 + 2
                lit = -x * 0.47 - y * 0.55 + nz * 0.66
                col = 1 if (int(math.floor(lon)) + int(math.floor(lat))) & 1 else 2      # white / red
                if lit < 0.24 or x * x + y * y > 0.8:
                    col += 2                                                             # the dark side
                if lit > 0.94:
                    col = 5
                pat[j * 16 + i] = col
        frames.append(pat)
    return frames


SPR_PAL = [0x000, rgb9(0xFFE), rgb9(0xE22), rgb9(0x999), rgb9(0x811), rgb9(0xFFF),
           rgb9(0x811), rgb9(0xE22), rgb9(0xFFE), rgb9(0x667), rgb9(0xCCD)]
S_RED_D, S_RED, S_RUBBER, S_METAL_D, S_METAL_L = 6, 7, 8, 9, 10


def flipper_frames(f, n=8):
    """the left flipper at n angles from rest to up, drawn like render.c's
    draw_flipper (three layers, red to rubber), as 32 x 32 around a fixed
    origin: (ox, oy) is the pivot's place in the 32 x 32"""
    side, fx, fy, ln, r1, r2, rest, up = f
    ox, oy = 6, 12
    frames = []
    for k in range(n):
        a = rest + (up - rest) * k / (n - 1)
        im = Canvas(32, 32, 0xE3)
        c, s = math.cos(a), math.sin(a)
        tx, ty = ln * c, ln * s
        for lvl, z in enumerate((0, 11, 22)):
            col = (S_RED_D, S_RED, S_RUBBER)[lvl]
            P = lambda x, y: (ox + x * KX, oy - y * KY - z * KZ)
            n1, n2 = (-s * r1, c * r1), (-s * r2, c * r2)
            im.poly([P(n1[0], n1[1]), P(tx + n2[0], ty + n2[1]), P(tx - n2[0], ty - n2[1]), P(-n1[0], -n1[1])], col)
            im.ellipse(*P(0, 0), r1 * KX, r1 * KY, col)
            im.ellipse(*P(tx, ty), r2 * KX, r2 * KY, col)
        im.ellipse(*(ox, oy - 22 * KZ), 0.6, 0.6, S_METAL_D)
        frames.append([im.im.getpixel((i % 32, i // 32)) for i in range(32 * 32)])
    return frames, ox, oy


def to_patterns(pix32):
    """a 32 x 32 image -> four 16 x 16 patterns: TL, TR, BL, BR"""
    out = []
    for qy in (0, 16):
        for qx in (0, 16):
            out.append([pix32[(qy + y) * 32 + qx + x] for y in range(16) for x in range(16)])
    return out


def main(path, n):
    d = json.load(open(path))
    theme = [0, 1, 2][int(n)]
    os.makedirs(os.path.join(ROOT, "gfx"), exist_ok=True)
    cv, lamps = draw_table(d, theme)
    draw_panel(cv, d)
    # Layer 2's 320 mode is column-major: byte x * 256 + y
    pix = cv.im.load()
    l2 = bytes(pix[x, y] for x in range(W) for y in range(H))
    for k in range(5):                    # five 16K banks (src/data.asm)
        open(os.path.join(ROOT, "gfx", f"l2_{n}_{k}.bin"), "wb").write(l2[k * 16384:(k + 1) * 16384])
    cv.im.putpalette(sum(([((c >> 6) & 7) * 36, ((c >> 3) & 7) * 36, (c & 7) * 36] for c in palette9(d, theme, lamps)), []))
    cv.im.save(os.path.join(ROOT, "gfx", f"l2_{n}.png"))
    # sprites (the same for every table)
    pats = ball_frames()
    fl, fox, foy = flipper_frames(d["flip"][0])
    for fr in fl:
        pats += to_patterns(fr)
    plunger = [0xE3] * 256
    for y in range(16):
        for x in range(6, 10):
            plunger[y * 16 + x] = S_METAL_L if y < 3 else S_METAL_D
    pats.append(plunger)
    open(os.path.join(ROOT, "gfx", "spr.bin"), "wb").write(bytes(sum(pats, [])))
    write_c(d, n, theme, lamps, fox, foy, len(pats))


def palette9(d, theme, lamps):
    pal = [rgb9(c) for c in COMMON]
    tp = THEME[theme]
    for pen, c in zip((P_FIELD1, P_FIELD2, P_ART1, P_ART2, P_ART3, P_CAB_D, P_CAB_L, P_SHADOW, P_WOOD), tp):
        pal[pen] = rgb9(c)
    pal += [0] * (256 - len(pal))
    for i, col in lamps:
        pal[I_LAMP + i] = pal[LAMP_OFF[col]]
    for i in range(8):
        pal[I_DROP + i] = pal[P_ORANGE]
    for i in range(10):
        pal[I_BUMPER + i] = pal[P_RED]
    pal[I_SLING] = pal[I_SLING + 1] = pal[P_RUBBER]
    pal[I_SPIN] = pal[I_SPIN + 1] = pal[P_METAL_L]
    for i, k in enumerate(("bg", "frame", "title", "text", "dim", "dmd_frame")):
        pal[I_PANEL + i] = rgb9(PANEL[k])
    # Layer 2's transparency is RGB 0xE3: keep every colour off it
    for i, c in enumerate(pal):
        if (c >> 1) == 0xE3:
            pal[i] = c ^ 2
    return pal


def write_c(d, n, theme, lamps, fox, foy, npat):
    pal = palette9(d, theme, lamps)
    lamp_on = [0] * 29
    lamp_off = [0] * 29
    for i, col in lamps:
        lamp_on[i] = pal[LAMP_ON[col]]
        lamp_off[i] = pal[LAMP_OFF[col]]
    out = [f"/* generated by tools/gen_gfx.py for {d['name']} - do not edit */",
           "#include <stdint.h>", '#include "gfx.h"', "",
           f"const uint16_t l2_pal_{n}[256] = {{"]
    for i in range(0, 256, 16):
        out.append("    " + ", ".join(str(c) for c in pal[i:i + 16]) + ",")
    out.append("};")
    out.append(f"const uint16_t lamp_on_{n}[L_LAMPS] = {{ " + ", ".join(map(str, lamp_on)) + " };")
    out.append(f"const uint16_t lamp_off_{n}[L_LAMPS] = {{ " + ", ".join(map(str, lamp_off)) + " };")
    out.append(f"const uint8_t lamp_used_{n}[L_LAMPS] = {{ " + ", ".join("1" if any(i == j for j, _ in lamps) else "0" for i in range(29)) + " };")
    if n == "0":
        out.append("const uint16_t spr_pal[16] = { " + ", ".join(map(str, SPR_PAL + [0] * (16 - len(SPR_PAL)))) + " };")
    open(os.path.join(ROOT, "src", f"gfx_{n}.c"), "w").write("\n".join(out) + "\n")
    hdr = f"""/* generated by tools/gen_gfx.py - do not edit: the view and the pictures' layout */
#ifndef GFX_H
#define GFX_H
#include <stdint.h>
/* the view (the original's, x {S}): screen x = OX + x KX, y = OY - y KY - z KZ (mm) */
#define VIEW_OX {OX}
#define VIEW_OY {OY}
#define VIEW_KX_P {round(KX / 16 * 65536)}   /* per P, / 65536 */
#define VIEW_KY_P {round(KY / 16 * 65536)}
#define VIEW_KZ_MM {round(KZ * 256)}       /* per mm, / 256 */
/* palette entries that animate */
#define I_LAMP {I_LAMP}
#define I_DROP {I_DROP}
#define I_BUMPER {I_BUMPER}
#define I_SLING {I_SLING}
#define I_SPIN {I_SPIN}
#define P_DMD_BG {P_DMD_BG}
#define P_DMD_ON {P_DMD_ON}
#define P_DMD_MID {P_DMD_MID}
#define P_ORANGE {P_ORANGE}
#define P_BLACK {P_BLACK}
#define P_FLASH {P_FLASH}
#define P_RED {P_RED}
#define P_RUBBER {P_RUBBER}
#define P_METAL_L {P_METAL_L}
#define P_METAL_D {P_METAL_D}
#define I_PANEL {I_PANEL}
#define L_LAMPS 29
/* the dot-matrix display's place on the screen */
#define DMD_X {DMD_X}
#define DMD_Y {DMD_Y}
#define DMD_W {DMD_W}
#define DMD_H {DMD_H}
/* sprite patterns (gfx/spr.bin): 0-7 the ball, 8.. the flipper frames
   (4 patterns each: TL TR BL BR), then the plunger */
#define SPR_BALL 0
#define SPR_FLIP 8
#define SPR_FLIP_FRAMES 8
#define SPR_PLUNGER {npat - 1}
#define SPR_NPAT {npat}
#define FLIP_OX {fox}                   /* the pivot in the flipper's 32 x 32 */
#define FLIP_OY {foy}
extern const uint16_t l2_pal_0[256], lamp_on_0[L_LAMPS], lamp_off_0[L_LAMPS], spr_pal[16];
extern const uint8_t lamp_used_0[L_LAMPS];
#endif
"""
    open(os.path.join(ROOT, "src", "gfx.h"), "w").write(hdr)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
