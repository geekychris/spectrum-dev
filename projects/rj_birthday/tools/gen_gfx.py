#!/usr/bin/env python3
"""RJ's Birthday Bash's pictures for the Spectrum Next, from the original's
rooms.c and draw.c.

The original draws its six rooms (THE FOYER .. THE LIVING ROOM, 320 pixels
each, 1920 in all) every frame from rectangles, lines and text in 16
colours, and its people, items and balls from rectangles. Here:

  the house      the six rooms with the original's rectangles and lines,
                 shaded in the Next's colours (port: each colour a ramp of
                 shades; walls, floors and furniture lit from above, wood,
                 tatami, carpet and wallpaper textures, a drop shadow under
                 the text), stored as column RLE (gfx/world_*.bin, banks
                 WORLD_BANK..): the Next copies the columns that scroll
                 into view into Layer 2
  the title      the original's title screen, RJ's portrait from
                 rj_head.jpg in full colour (port: the original dithers it
                 to its 16 colours at 32 x 40), the same RLE
  the HUD bar    rows 0..15 (draw_hud's bar)
  sprites        RJ, the guests (their colour by palette offset), the pot
                 police, the items, the Boing ball (its 6 phases), smoke
                 puffs, want bubbles, patience bars (gfx/spr.bin)
  src/gfx_data.c/.h  palettes, where the columns are, the pattern slots
  build/world.png, build/title.png  previews

    python3 tools/gen_gfx.py
"""
import os
import re
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
ROOT = os.path.join(PROJ, "..", "..")
sys.path.insert(0, os.path.join(ROOT, "sdk", "tools"))
from nextpic import TRANSPARENT, group_patterns, rgb9_from_rgb  # noqa: E402
from l2pic import pack  # noqa: E402

SCREEN_W, SCREEN_H, ROOM_W, ROOMS = 320, 256, 320, 6
WORLD_W = ROOM_W * ROOMS
HUD_H, FLOOR_Y = 16, 210
TAPPER_LANE_Y0, TAPPER_LANE_H, SUSHI_LANE_Y0, SUSHI_LANE_H = 170, 22, 165, 24
WORLD_BANK = 16                       # 8K pages 32.. : the house's columns, then the title's

# the original's palette (main.c), 12-bit Amiga colours, and its names
AMIGA = [0x113, 0xFFF, 0x830, 0x520, 0xF00, 0x0A0, 0x23C, 0x8CF, 0xFE0, 0xF80, 0xF5A, 0x6E6, 0xDA8, 0x888, 0x800, 0xFF8]
(BG, WHITE, BROWN, DKBROWN, RED, GREEN, BLUE, LTBLUE, YELLOW, ORANGE, PINK, LTGREEN, TAN, GREY, DKRED,
 BTYELLOW) = range(16)
SHADES = 5                            # each colour: 0 darkest .. 2 the original's .. 4 lightest


def rgb(c12):
    return tuple(((c12 >> s) & 15) * 17 for s in (8, 4, 0))


def shade_rgb(c, s):
    r, g, b = rgb(AMIGA[c])
    f = [0.55, 0.78, 1.0, 1.12, 1.25][s]
    if f <= 1:
        return (int(r * f), int(g * f), int(b * f))
    return tuple(min(255, int(v + (255 - v) * (f - 1) * 0.9)) for v in (r, g, b))


def P(c, s=2):
    """Layer 2 index of colour c (the original's COL_*) in shade s"""
    return 1 + c * SHADES + s


def l2_palette():
    pal = [(0, 0, 0)]
    for c in range(16):
        for s in range(SHADES):
            pal.append(shade_rgb(c, s))
    return pal


# ---- font5 (sdk/src/font5.c), the text in the pictures ----
def font5():
    src = open(os.path.join(ROOT, "sdk", "src", "font5.c")).read()
    rows = re.findall(r"\{ (0x[0-9A-F]{2}(?:, 0x[0-9A-F]{2}){6}) \}", src)
    return {chr(33 + i): [int(v, 16) for v in r.split(", ")] for i, r in enumerate(rows)}


FONT = font5()


class Canvas:
    def __init__(self, w, h, fill=0):
        self.a = np.full((h, w), fill, np.int32)
        self.w, self.h = w, h

    def rect(self, x1, y1, x2, y2, c, ox=0, style="flat"):
        """room_rect: x1..x2, y1..y2 inclusive, clipped to the HUD's bottom;
        port: styles shade it (panel: lit edges; band: darker down the
        rectangle in 4-pixel bands)"""
        x1, x2 = max(0, x1 + ox), min(self.w - 1, x2 + ox)
        y1, y2 = max(HUD_H, y1), min(self.h - 1, y2)
        if x1 > x2 or y1 > y2:
            return
        if style == "flat":
            self.a[y1:y2 + 1, x1:x2 + 1] = P(c)
        elif style == "band":
            for y in range(y1, y2 + 1):
                k = (y - y1) // 4
                n = max(1, (y2 - y1 + 1) // 4)
                self.a[y, x1:x2 + 1] = P(c, 3 if k == 0 else 2 if k < n * 0.85 else 1)
        elif style == "panel":
            self.a[y1:y2 + 1, x1:x2 + 1] = P(c)
            if y2 > y1:
                self.a[y1, x1:x2 + 1] = P(c, 3)
                self.a[y2, x1:x2 + 1] = P(c, 1)
            if x2 > x1:
                self.a[y1:y2 + 1, x1] = P(c, 3)
                self.a[y1:y2 + 1, x2] = P(c, 1)

    def put(self, x, y, v):
        if 0 <= x < self.w and HUD_H <= y < self.h:
            self.a[y, x] = v

    def line(self, x1, y1, x2, y2, c, ox=0):
        """room_line (Move/Draw)"""
        n = max(abs(x2 - x1), abs(y2 - y1), 1)
        for k in range(n + 1):
            self.put(ox + x1 + (x2 - x1) * k // n, y1 + (y2 - y1) * k // n, P(c))

    def text(self, x, y, s, c, ox=0, shadow=True):
        """room_text: y is the baseline (topaz 8); port: font5, a shadow"""
        top = y - 6
        for i, ch in enumerate(s):
            g = FONT.get(ch.upper())
            if not g:
                continue
            for r, bits in enumerate(g):
                for b in range(5):
                    if bits & (0x80 >> b):
                        xx = ox + x + i * 8 + b + 1
                        if shadow:
                            self.put(xx + 1, top + r + 1, 0)
                        self.put(xx, top + r, P(c))


# ---- the rooms (rooms.c), port: shaded ----
def foyer(cv, sx):
    cv.rect(0, FLOOR_Y, 319, SCREEN_H - 1, DKRED, sx, "band")             # floor: carpet
    for x in range(0, 320, 4):                                             # port: carpet weave
        for y in range(FLOOR_Y + 2, SCREEN_H, 4):
            cv.put(sx + x + (y // 4) % 2 * 2, y, P(DKRED, 1))
    cv.rect(0, HUD_H, 319, FLOOR_Y - 1, TAN, sx, "band")                   # back wall
    for x in range(0, 320, 16):                                            # port: wallpaper stripes
        cv.rect(x, HUD_H, x + 1, FLOOR_Y - 21, TAN, sx)
        for y in range(HUD_H, FLOOR_Y - 20):
            cv.put(sx + x, y, P(TAN, 3))
    cv.rect(0, FLOOR_Y - 20, 319, FLOOR_Y - 1, BROWN, sx, "panel")         # wainscoting
    cv.rect(290, HUD_H + 10, 310, FLOOR_Y - 1, DKBROWN, sx, "panel")       # door frame
    cv.rect(295, HUD_H + 15, 305, FLOOR_Y - 5, LTBLUE, sx, "band")
    cv.rect(270, FLOOR_Y + 2, 320, FLOOR_Y + 10, GREEN, sx, "panel")       # welcome mat
    cv.rect(10, FLOOR_Y - 35, 60, FLOOR_Y - 25, BROWN, sx, "panel")        # gift table
    cv.rect(15, FLOOR_Y - 34, 20, FLOOR_Y - 1, DKBROWN, sx, "panel")
    cv.rect(50, FLOOR_Y - 34, 55, FLOOR_Y - 1, DKBROWN, sx, "panel")
    cv.text(80, HUD_H + 30, "HAPPY 70TH!", YELLOW, sx)                     # banner
    for bx, by, c in ((40, 8, RED), (140, 5, BLUE), (240, 8, YELLOW)):      # balloons
        cv.rect(bx, HUD_H + by, bx + 8, HUD_H + by + 10, c, sx, "panel")
        cv.line(bx + 4, HUD_H + by + 10, bx + 4, HUD_H + by + 27, WHITE, sx)


def amsterdam(cv, sx):
    cv.rect(0, FLOOR_Y, 319, SCREEN_H - 1, DKBROWN, sx, "band")            # floor: dark wood
    for x in range(0, 320, 24):                                            # port: planks
        cv.line(x, FLOOR_Y, x, SCREEN_H - 1, BG, sx)
    cv.rect(0, HUD_H, 319, FLOOR_Y - 1, GREEN, sx, "band")                 # hazy green walls
    cv.rect(50, HUD_H + 20, 100, HUD_H + 50, LTGREEN, sx, "band")          # haze
    cv.rect(180, HUD_H + 30, 230, HUD_H + 55, LTGREEN, sx, "band")
    for lane in range(3):                                                  # the bar: 3 lanes
        ly = TAPPER_LANE_Y0 + lane * TAPPER_LANE_H
        cv.rect(10, ly, 310, ly + 3, BROWN, sx, "panel")
        cv.rect(10, ly + 4, 310, ly + 6, DKBROWN, sx, "panel")
    cv.rect(0, HUD_H + 60, 20, FLOOR_Y - 1, BROWN, sx, "panel")            # shelves
    cv.rect(3, HUD_H + 65, 17, HUD_H + 75, ORANGE, sx, "panel")
    cv.rect(3, HUD_H + 80, 17, HUD_H + 90, ORANGE, sx, "panel")
    cv.text(100, HUD_H + 25, "COFFEE SHOP", BTYELLOW, sx)
    cv.rect(280, HUD_H + 50, 285, HUD_H + 60, RED, sx, "panel")            # tulip
    cv.rect(282, HUD_H + 60, 283, HUD_H + 75, GREEN, sx)


def tokyo(cv, sx):
    cv.rect(0, FLOOR_Y, 319, SCREEN_H - 1, TAN, sx, "band")                # tatami
    for x in range(0, 320, 40):                                            # port: tatami edges
        cv.rect(x, FLOOR_Y, x + 1, SCREEN_H - 1, TAN, sx)
        for y in range(FLOOR_Y, SCREEN_H):
            cv.put(sx + x, y, P(TAN, 0))
    cv.rect(0, HUD_H, 319, FLOOR_Y - 1, WHITE, sx, "band")                 # paper screens
    for x in (80, 160, 240):
        cv.line(x, HUD_H, x, FLOOR_Y - 1, TAN, sx)
        cv.line(x + 1, HUD_H, x + 1, FLOOR_Y - 1, BROWN, sx)
    for y in (HUD_H + 60, HUD_H + 120):
        cv.line(0, y, 319, y, TAN, sx)
    for lane in range(3):                                                  # conveyor belts
        ly = SUSHI_LANE_Y0 + lane * SUSHI_LANE_H
        cv.rect(10, ly, 310, ly + 2, GREY, sx, "panel")
        cv.rect(10, ly + 14, 310, ly + 16, GREY, sx, "panel")
    cv.rect(150, HUD_H + 10, 170, HUD_H + 30, RED, sx, "panel")           # lantern
    cv.line(160, HUD_H, 160, HUD_H + 10, DKBROWN, sx)
    cv.text(90, HUD_H + 45, "SUSHI BAR", RED, sx)


def silicon(cv, sx):
    cv.rect(0, FLOOR_Y, 319, SCREEN_H - 1, GREY, sx, "band")               # grey carpet
    cv.rect(0, HUD_H, 319, FLOOR_Y - 1, BLUE, sx, "band")                  # blue walls
    for x0, label, c in ((20, "ARC", RED), (65, "PLT", ORANGE)):           # the arcade cabinets
        cv.rect(x0, FLOOR_Y - 60, x0 + 35, FLOOR_Y - 1, DKBROWN, sx, "panel")
        cv.rect(x0 + 5, FLOOR_Y - 55, x0 + 30, FLOOR_Y - 25, BG, sx, "panel")
        cv.text(x0 + 6 + (1 if label == "PLT" else 0), FLOOR_Y - 40, label, c, sx)
    for x0, inner, label, lc in ((140, WHITE, "A1000", BLUE), (200, GREY, "LYNX", RED), (260, BG, "3DO", BTYELLOW)):
        cv.rect(x0, FLOOR_Y - 45, x0 + 35, FLOOR_Y - 20, TAN, sx, "panel")  # display cases
        cv.rect(x0 + 5, FLOOR_Y - 43, x0 + 30, FLOOR_Y - 22, inner, sx, "panel")
        cv.text(x0 + (3 if label == "A1000" else 5 if label == "LYNX" else 8), FLOOR_Y - 30, label, lc, sx)
    cv.rect(130, HUD_H + 10, 190, HUD_H + 30, BTYELLOW, sx, "panel")       # posters
    cv.text(135, HUD_H + 24, "AMIGA", BLUE, sx)
    cv.rect(230, HUD_H + 10, 290, HUD_H + 30, LTBLUE, sx, "panel")
    cv.text(240, HUD_H + 24, "1985", WHITE, sx)


def bayarea(cv, sx):
    cv.rect(0, FLOOR_Y, 319, SCREEN_H - 1, TAN, sx, "band")                # patio
    for x in range(0, 320, 20):                                            # port: paving
        cv.line(x, FLOOR_Y, x, SCREEN_H - 1, BROWN, sx)
    for y in range(FLOOR_Y + 10, SCREEN_H, 12):
        cv.line(0, y, 319, y, BROWN, sx)
    cv.rect(0, HUD_H, 319, FLOOR_Y - 1, LTBLUE, sx, "band")                # sky
    cv.rect(30, HUD_H + 20, 290, HUD_H + 25, RED, sx, "panel")             # the Golden Gate
    cv.rect(60, HUD_H + 5, 65, HUD_H + 25, RED, sx, "panel")
    cv.rect(230, HUD_H + 5, 235, HUD_H + 25, RED, sx, "panel")
    for i in range(10):
        cv.line(62, HUD_H + 5, 60 + i * 19, HUD_H + 18, ORANGE, sx)
    cv.rect(0, HUD_H + 30, 319, HUD_H + 50, BLUE, sx, "band")              # water
    for x in range(4, 320, 12):                                            # port: waves
        cv.line(x, HUD_H + 36 + (x // 12) % 3 * 4, x + 4, HUD_H + 36 + (x // 12) % 3 * 4, LTBLUE, sx)
    for x0 in (50, 150, 250):                                              # patio tables
        cv.rect(x0, FLOOR_Y - 15, x0 + 30, FLOOR_Y - 5, BROWN, sx, "panel")
    cv.text(120, HUD_H + 60, "SAN FRANCISCO", YELLOW, sx)
    cv.line(0, FLOOR_Y - 25, 319, FLOOR_Y - 25, BTYELLOW, sx)              # the scooter lane


def living(cv, sx):
    cv.rect(0, FLOOR_Y, 319, SCREEN_H - 1, DKRED, sx, "band")              # dance floor
    for cy in range(FLOOR_Y, SCREEN_H, 16):
        for cx in range(0, 320, 16):
            if ((cx // 16) + (cy // 16)) & 1:
                cv.rect(cx, cy, cx + 15, cy + 15, GREY, sx, "panel")
    cv.rect(0, HUD_H, 319, FLOOR_Y - 1, PINK, sx, "band")                  # walls
    cv.rect(152, HUD_H + 5, 168, HUD_H + 21, WHITE, sx, "panel")           # disco ball
    cv.rect(155, HUD_H + 8, 165, HUD_H + 18, GREY, sx, "panel")
    cv.line(160, HUD_H, 160, HUD_H + 5, GREY, sx)
    cv.rect(140, FLOOR_Y - 30, 180, FLOOR_Y - 1, WHITE, sx, "panel")       # the cake
    cv.rect(142, FLOOR_Y - 32, 178, FLOOR_Y - 30, PINK, sx)
    cv.rect(142, FLOOR_Y - 20, 178, FLOOR_Y - 18, PINK, sx)
    for x in (150, 160, 170):                                              # candles, flames
        cv.rect(x, FLOOR_Y - 38, x + 2, FLOOR_Y - 32, BTYELLOW, sx)
        cv.rect(x, FLOOR_Y - 41, x + 2, FLOOR_Y - 38, ORANGE, sx)
    cv.text(153, FLOOR_Y - 10, "70", RED, sx)
    cv.line(20, HUD_H + 10, 80, HUD_H + 25, YELLOW, sx)                    # streamers
    cv.line(240, HUD_H + 10, 300, HUD_H + 25, YELLOW, sx)
    cv.line(50, HUD_H + 15, 120, HUD_H + 5, RED, sx)
    cv.line(200, HUD_H + 15, 270, HUD_H + 5, RED, sx)


ROOMS_DRAW = [foyer, amsterdam, tokyo, silicon, bayarea, living]
ROOM_NAMES = ["THE FOYER", "AMSTERDAM LOUNGE", "TOKYO ROOM", "SILICON VALLEY DEN", "BAY AREA PATIO", "THE LIVING ROOM"]


def world():
    cv = Canvas(WORLD_W, SCREEN_H)
    for r, f in enumerate(ROOMS_DRAW):
        f(cv, r * ROOM_W)
    return cv.a


def hud_bar():
    """draw_hud's bar: COL_DKBROWN, rows 0..15 (port: lit edges)"""
    a = np.full((HUD_H, SCREEN_W), P(DKBROWN), np.int32)
    a[0, :] = P(DKBROWN, 3)
    a[HUD_H - 1, :] = P(DKBROWN, 0)
    return a


def title():
    """draw_title: the border, RJ (hat, head, purple shirt, arms, hands);
    the words are the tilemap's (they blink and change). port: the head is
    rj_head.jpg at 40 x 50 in its own colours, the shapes shaded"""
    cv = Canvas(SCREEN_W, SCREEN_H, P(BG))
    cv.a[:, :] = P(BG)
    for (x1, y1, x2, y2) in ((30, 30, 290, 32), (30, 30, 32, 220), (30, 218, 290, 220), (288, 30, 290, 220)):
        cv.a[y1:y2 + 1, x1:x2 + 1] = P(BTYELLOW)
    # the party hat: a triangle from (160, 72) to (146..174, 86), pom-pom
    for y in range(72, 87):
        w = (y - 72) * 14 // 14
        cv.a[y, 160 - w:160 + w + 1] = P(RED, 2 if (y // 3) % 2 else 3)
    cv.a[68:72, 157:164] = P(BTYELLOW)
    # the body (pink shirt), collar, arms, hands - draw_title's, under the bigger head
    cv.a[136:166, 146:175] = P(PINK)
    cv.a[136:166, 146] = P(PINK, 3)
    cv.a[136:141, 154:158] = P(WHITE)
    cv.a[136:141, 163:167] = P(WHITE)
    cv.a[139:157, 137:146] = P(PINK, 1)
    cv.a[139:157, 175:184] = P(PINK, 1)
    cv.a[157:162, 137:146] = P(TAN)
    cv.a[157:162, 175:184] = P(TAN)
    return cv.a


def head_rgb():
    img = Image.open(os.path.join(HERE, "orig", "rj_head.jpg")).convert("RGB")
    w, h = img.size
    s = min(w, h * 4 // 5)                 # a 4:5 crop round the middle
    img = img.crop(((w - s) // 2, max(0, (h - s * 5 // 4) // 3), (w + s) // 2, max(0, (h - s * 5 // 4) // 3) + s * 5 // 4))
    return img.resize((40, 50), Image.LANCZOS)


# ---- sprites (draw.c) ----
# sprite palette: blocks of 16. Blocks 0..7: the guests (offset = their
# colour) and the want bubbles (offset = the item wanted) share them:
#   1 skin, 2 skin shade, 3 legs, 4..6 the guest's colour (light, base,
#   dark), 7 happy mouth, 8 angry brows, 9 bubble white, 10 the want's colour
# 128..: the original's 16 colours in 3 shades (S(c, s)); 208/224/240:
# smoke puffs (offset 0 white, 1 grey, 2 light green)
GUEST_COLS = [RED, GREEN, BLUE, ORANGE, PINK, LTGREEN, YELLOW, LTBLUE]      # game.c guest_colors
WANT_COLS = [GREY, RED, DKBROWN, RED, BLUE, GREEN, YELLOW, LTBLUE]          # by item type 0..7 (draw.c)
PUFF = 208


def S(c, s=1):
    return 128 + c * 3 + s


def spr_palette():
    pal = [(0, 0, 0)] * 256
    for k in range(8):
        b = k * 16
        pal[b + 1] = shade_rgb(TAN, 2)
        pal[b + 2] = shade_rgb(TAN, 1)
        pal[b + 3] = shade_rgb(BLUE, 2)
        pal[b + 4] = shade_rgb(GUEST_COLS[k], 3)
        pal[b + 5] = shade_rgb(GUEST_COLS[k], 2)
        pal[b + 6] = shade_rgb(GUEST_COLS[k], 1)
        pal[b + 7] = shade_rgb(BTYELLOW, 2)
        pal[b + 8] = shade_rgb(RED, 2)
        pal[b + 9] = shade_rgb(WHITE, 2)
        pal[b + 10] = shade_rgb(WANT_COLS[k], 2)
    for c in range(16):
        for s in range(3):
            pal[S(c, s)] = shade_rgb(c, [1, 2, 3][s])
    pal[PUFF] = shade_rgb(WHITE, 2)
    pal[PUFF + 16] = shade_rgb(GREY, 3)
    pal[PUFF + 32] = shade_rgb(LTGREEN, 3)
    return pal


class Spr:
    """a sprite image of w x h; (ox, oy) is where the original's (sx, sy) is"""
    def __init__(self, w, h, ox, oy):
        self.a = np.full((h, w), -1, np.int32)
        self.ox, self.oy = ox, oy

    def rect(self, x1, y1, x2, y2, v):
        """RectFill in the original's coordinates (relative to sx, sy)"""
        for y in range(y1, y2 + 1):
            for x in range(x1, x2 + 1):
                self.put(x, y, v)

    def put(self, x, y, v):
        x, y = x + self.ox, y + self.oy
        if 0 <= y < self.a.shape[0] and 0 <= x < self.a.shape[1]:
            self.a[y, x] = v

    def line(self, x1, y1, x2, y2, v):
        n = max(abs(x2 - x1), abs(y2 - y1), 1)
        for k in range(n + 1):
            self.put(x1 + (x2 - x1) * k // n, y1 + (y2 - y1) * k // n, v)

    def lit(self, x1, y1, x2, y2, c):
        """a rectangle in colour c, port: lit from the top left"""
        self.rect(x1, y1, x2, y2, S(c, 1))
        self.rect(x1, y1, x2, y1, S(c, 2))
        self.rect(x1, y1, x1, y2, S(c, 2))
        if x2 > x1 + 1 and y2 > y1 + 1:
            self.rect(x2, y1 + 1, x2, y2, S(c, 0))


PLAYER_H, GUEST_H, COP_H = 24, 16, 18


def rj(frame):
    """draw_player (the hat, head, glasses, body, legs)"""
    s = Spr(16, 32, 0, 29)
    for y in range(-PLAYER_H, -PLAYER_H + 9):                  # the party hat (filled; the original's outline)
        w = (y + PLAYER_H) * 4 // 8
        s.rect(6 - w, y, 6 + w, y, S(RED, 1 if (y // 2) % 2 else 2))
    s.rect(5, -PLAYER_H - 2, 7, -PLAYER_H, S(BTYELLOW, 2))
    s.lit(3, -PLAYER_H + 8, 9, -PLAYER_H + 15, TAN)            # head
    s.rect(3, -PLAYER_H + 10, 5, -PLAYER_H + 12, S(BLUE, 1))   # glasses
    s.rect(7, -PLAYER_H + 10, 9, -PLAYER_H + 12, S(BLUE, 1))
    s.line(5, -PLAYER_H + 11, 7, -PLAYER_H + 11, S(BLUE, 1))
    s.lit(4, -PLAYER_H + 16, 8, -6, LTBLUE)                    # body
    if frame & 1:                                              # legs
        s.rect(4, -5, 5, 0, S(BLUE, 1))
        s.rect(7, -5, 8, 0, S(BLUE, 1))
    else:
        s.rect(3, -5, 4, 0, S(BLUE, 1))
        s.rect(8, -5, 9, 0, S(BLUE, 1))
    return s


def guest(state):
    """draw_one_guest: 0 normal, 1 happy, 2 angry (indices into blocks 0..7)"""
    s = Spr(16, 32, 3, 29)
    s.rect(2, -GUEST_H, 8, -GUEST_H + 6, 1)                    # head
    s.rect(8, -GUEST_H + 1, 8, -GUEST_H + 6, 2)
    s.rect(1, -GUEST_H - 1, 9, -GUEST_H + 1, 5)                # hair / hat
    s.rect(1, -GUEST_H - 1, 9, -GUEST_H - 1, 4)
    s.rect(3, -GUEST_H + 7, 7, -3, 5)                          # body
    s.rect(3, -GUEST_H + 7, 3, -3, 4)
    s.rect(7, -GUEST_H + 8, 7, -3, 6)
    s.rect(3, -2, 4, 0, 3)                                     # legs
    s.rect(6, -2, 7, 0, 3)
    if state == 1:                                             # happy face
        s.line(3, -GUEST_H + 4, 4, -GUEST_H + 5, 7)
        s.line(4, -GUEST_H + 5, 6, -GUEST_H + 5, 7)
        s.line(6, -GUEST_H + 5, 7, -GUEST_H + 4, 7)
    if state == 2:                                             # angry
        s.line(2, -GUEST_H + 3, 4, -GUEST_H + 5, 8)
        s.line(8, -GUEST_H + 3, 6, -GUEST_H + 5, 8)
    return s


def want_bubble():
    """the item-request bubble: a white box, a tail, the want's colour (offset = item)"""
    s = Spr(16, 16, 2, 0)
    s.rect(-2, 0, 10, 8, 9)
    s.line(3, 9, 5, 11, 9)
    s.rect(1, 1, 7, 7, 10)
    return s


def patience(level):
    """the patience bar, 11 x 2: red, green up to level/5 of it"""
    s = Spr(16, 16, 0, 0)
    s.rect(0, 0, 10, 1, S(RED, 1))
    filled = level * 10 // 5
    if filled > 0:
        s.rect(0, 0, filled, 1, S(GREEN, 2))
    return s


def cop(frame, chase):
    """draw_cops"""
    s = Spr(16, 32, 3, 31)
    s.rect(1, -COP_H, 9, -COP_H + 3, S(BLUE, 1))
    s.rect(1, -COP_H, 9, -COP_H, S(BLUE, 2))
    s.rect(0, -COP_H + 3, 10, -COP_H + 4, S(BLUE, 0))
    s.rect(4, -COP_H + 1, 6, -COP_H + 2, S(BTYELLOW, 1))
    s.lit(2, -COP_H + 5, 8, -COP_H + 10, TAN)
    s.rect(3, -COP_H + 7, 4, -COP_H + 8, S(RED, 1))
    s.rect(6, -COP_H + 7, 7, -COP_H + 8, S(RED, 1))
    s.lit(3, -COP_H + 11, 7, -5, BLUE)
    s.rect(4, -COP_H + 12, 6, -COP_H + 14, S(BTYELLOW, 1))
    if frame & 1:
        s.rect(3, -4, 4, 0, S(BLUE, 0))
        s.rect(6, -4, 7, 0, S(BLUE, 0))
    else:
        s.rect(2, -4, 3, 0, S(BLUE, 0))
        s.rect(7, -4, 8, 0, S(BLUE, 0))
    if chase:
        s.rect(4, -COP_H - 6, 6, -COP_H - 2, S(RED, 2))
        s.rect(4, -COP_H - 1, 6, -COP_H, S(RED, 2))
    return s


def item(t):
    """draw_items, by item type (1 gift .. 10 scooter; 7 demo is the default)"""
    if t == 10:                                                # scooter: 16 x 32
        s = Spr(16, 32, 2, 29)
        s.rect(0, -10, 2, -2, S(GREY, 1))
        s.rect(-1, -11, 3, -10, S(GREY, 2))
        s.rect(0, -2, 10, -1, S(GREY, 1))
        s.rect(-1, -1, 1, 1, S(BG, 1))
        s.rect(9, -1, 11, 1, S(BG, 1))
        s.lit(3, -16, 7, -4, DKBROWN)
        s.rect(4, -19, 6, -16, S(TAN, 1))
        return s
    s = Spr(16, 16, 3, 13)
    if t == 1:
        s.lit(0, -8, 8, 0, RED)
        s.line(4, -8, 4, 0, S(BTYELLOW, 1))
        s.line(0, -4, 8, -4, S(BTYELLOW, 1))
        s.rect(2, -10, 6, -8, S(YELLOW, 2))
    elif t == 2:
        s.rect(0, -4, 8, 0, S(DKBROWN, 1))
        s.rect(1, -3, 7, -1, S(BROWN, 1))
        s.rect(1, -3, 7, -3, S(BROWN, 2))
    elif t in (3, 4, 5):
        s.rect(0, -5, 7, 0, S(WHITE, 1))
        s.rect(1, -4, 6, -1, S([RED, BLUE, GREEN][t - 3], 1))
        s.rect(1, -4, 6, -4, S([RED, BLUE, GREEN][t - 3], 2))
    elif t == 6:
        s.lit(0, -8, 8, 0, YELLOW)
        s.rect(1, -7, 7, -1, S(ORANGE, 1))
        s.line(4, -10, 4, -8, S(RED, 1))
        s.rect(2, -10, 6, -9, S(RED, 1))
    elif t == 9:
        s.rect(0, -6, 7, 0, S(WHITE, 1))
        s.rect(0, -7, 7, -6, S(PINK, 1))
        s.rect(3, -9, 5, -7, S(RED, 1))
    elif t == 8:
        s.rect(2, -8, 5, 0, S(GREEN, 1))
        s.rect(0, -7, 7, -5, S(GREEN, 1))
        s.rect(1, -9, 6, -3, S(GREEN, 1))
        s.rect(2, -7, 5, -5, S(LTGREEN, 1))
    else:
        s.rect(0, -4, 6, 0, S(WHITE, 1))
    return s


def boing(phase):
    """draw_boings: a ball of radius 8 (port: 7, a 16 x 16 sprite), bands
    of 3 rows by stripes of 3 columns, red and white, shifted by the phase"""
    s = Spr(16, 16, 8, 8)
    r = 7
    for dy in range(-r, r + 1):
        dsq = r * r - dy * dy
        hw = 0
        while (hw + 1) * (hw + 1) <= dsq:
            hw += 1
        if hw <= 0:
            continue
        hband = ((dy + phase) // 3) & 1
        for dx in range(-hw, hw + 1):
            col = ((dx + phase) // 3) & 1 ^ hband
            lit = 2 if dx + dy < -4 else 1 if dx + dy < 6 else 0
            s.put(dx, dy, S(RED if col else WHITE, lit))
    return s


def boing_shadow():
    s = Spr(16, 16, 8, 0)
    s.rect(-4, 1, 4, 2, S(DKRED, 0))
    return s


def puff(size):
    s = Spr(16, 16, 8, 8)
    s.rect(-size, -size, size, size, PUFF)
    return s


def spark():
    s = Spr(16, 16, 0, 0)
    s.rect(0, 0, 2, 2, S(RED, 2))
    return s


def sprites():
    """[(name, Spr)] in pattern order"""
    out = [("rj0", rj(0)), ("rj1", rj(1))]
    out += [(f"guest{k}", guest(k)) for k in range(3)]
    out += [("want", want_bubble())]
    out += [(f"patience{k}", patience(k)) for k in range(6)]
    out += [(f"cop{f}{c}", cop(f, c)) for c in range(2) for f in range(2)]
    out += [(f"item{t}", item(t)) for t in range(1, 11)]
    out += [(f"boing{p}", boing(p)) for p in range(6)]
    out += [("bshadow", boing_shadow())]
    out += [(f"puff{k}", puff(k)) for k in range(2, 6)]
    out += [("spark", spark())]
    return out


def to_idx(a):
    return np.where(a < 0, TRANSPARENT, a).astype(np.uint8)


def write_c(pal9, spal9, world_where, title_where, slots, head_cols):
    with open(os.path.join(PROJ, "src", "gfx_data.c"), "w") as f:
        f.write('/* made by tools/gen_gfx.py - do not edit */\n#include "gfx_data.h"\n\n')
        f.write(f"const uint16_t l2_pal[{len(pal9)}] = {{\n")
        for i in range(0, len(pal9), 16):
            f.write("    " + ", ".join(f"0x{c:03X}" for c in pal9[i:i + 16]) + ",\n")
        f.write("};\nconst uint16_t spr_pal[256] = {\n")
        for i in range(0, 256, 16):
            f.write("    " + ", ".join(f"0x{c:03X}" for c in spal9[i:i + 16]) + ",\n")
        f.write("};\n")
        f.write("const char *const room_names[6] = { " + ", ".join(f'"{n}"' for n in ROOM_NAMES) + " };\n")
        f.write("const uint8_t sprite_anchor[SPR_IMAGES][2] = {\n")
        for name, (slot, c, r, ox, oy) in slots.items():
            f.write(f"    {{ {ox}, {oy} }},   /* {name} */\n")
        f.write("};\n")
    with open(os.path.join(PROJ, "src", "gfx_data.h"), "w") as f:
        f.write("/* made by tools/gen_gfx.py - do not edit */\n#ifndef GFX_DATA_H\n#define GFX_DATA_H\n#include <stdint.h>\n\n")
        f.write(f"#define WORLD_BANK {WORLD_BANK}     /* the house's columns, column RLE: banks WORLD_BANK.. */\n")
        f.write(f"#define INDEX_PAGE {2 * WORLD_BANK + 0}  /* the column index (see gen_gfx.py) */\n")
        f.write(f"#define WORLD_COLS {WORLD_W}\n#define SPR_BANK {SPR_BANK}\n#define SPR_PATTERNS {len(slots)}\n")
        f.write(f"#define L2_COLOURS {len(pal9)}\n#define PUFF_INDEX {PUFF}\n")
        for i, name in enumerate(slots):
            f.write(f"#define SP_{name.upper()} {i}\n")
        f.write(f"#define SPR_IMAGES {len(slots)}\n")
        f.write("extern const uint16_t l2_pal[L2_COLOURS];\nextern const uint16_t spr_pal[256];\n")
        f.write("extern const char *const room_names[6];\n")
        f.write("extern const uint8_t sprite_anchor[SPR_IMAGES][2];   /* where (sx, sy) is in each image */\n")
        f.write("#endif\n")


SPR_BANK = 0                          # set in main(): after the pictures


def main():
    global SPR_BANK
    os.makedirs(os.path.join(PROJ, "build"), exist_ok=True)
    os.makedirs(os.path.join(PROJ, "gfx"), exist_ok=True)
    pal = l2_palette()
    w = world()
    t = title()
    # RJ's head: its own colours (quantized to 48), after the shades
    head = head_rgb()
    q = head.quantize(colors=48, method=Image.Quantize.MEDIANCUT)
    hp = q.getpalette()[:48 * 3]
    base = len(pal)
    for i in range(48):
        pal.append(tuple(hp[i * 3:i * 3 + 3]))
    hq = np.array(q, np.int32)
    t[86:136, 140:180] = hq + base
    pal9 = [rgb9_from_rgb(*c) for c in pal]
    pal9 = [c ^ 2 if (c >> 1) == TRANSPARENT else c for c in pal9]
    hud = hud_bar()
    # the HUD bar's 16 rows ride on top of every column the Next copies (rows 16.. are RLE)
    first = 2 * WORLD_BANK + 2        # page 2 * WORLD_BANK: the index, + 1: the HUD bar
    wcols = [w[:, x] for x in range(WORLD_W)]
    tcols = [t[:, x] for x in range(SCREEN_W)]
    pages, index = pack(wcols + tcols, HUD_H, first)       # the title's columns follow the house's
    world_where = title_where = None
    assert len(index) <= 8192, len(index)
    hudpage = bytes(hud.T.astype(np.uint8).tobytes())  # the HUD bar, column-major, 320 x 16
    allpages = [index, hudpage] + pages
    if len(allpages) % 2:
        allpages.append(b"")
    nbanks = len(allpages) // 2
    SPR_BANK = WORLD_BANK + nbanks
    asm = ["; made by tools/gen_gfx.py: the pictures and the sprite patterns, straight into banks by the .nex"]
    for f_ in os.listdir(os.path.join(PROJ, "gfx")):
        if f_.endswith(".bin"):
            os.remove(os.path.join(PROJ, "gfx", f_))
    for k in range(nbanks):
        with open(os.path.join(PROJ, "gfx", f"bank_{WORLD_BANK + k}.bin"), "wb") as fo:
            fo.write(bytes(allpages[2 * k]).ljust(8192, b"\0") + bytes(allpages[2 * k + 1]).ljust(8192, b"\0"))
        asm += [f"SECTION BANK_{WORLD_BANK + k}", f'BINARY "gfx/bank_{WORLD_BANK + k}.bin"']
    # sprites
    spal = spr_palette()
    spal9 = [rgb9_from_rgb(*c) for c in spal]
    pats, slots = bytearray(), {}
    for name, s in sprites():
        data, c, r = group_patterns(to_idx(s.a))
        slots[name] = (len(pats) // 256, c, r, s.ox, s.oy)
        pats += data
    assert len(pats) <= 64 * 256, len(pats) // 256
    with open(os.path.join(PROJ, "gfx", f"bank_{SPR_BANK}.bin"), "wb") as fo:
        fo.write(bytes(pats))
    asm += [f"SECTION BANK_{SPR_BANK}", f'BINARY "gfx/bank_{SPR_BANK}.bin"']
    with open(os.path.join(PROJ, "src", "gfx_data.asm"), "w") as fo:
        fo.write("\n".join(asm) + "\n")
    # slot numbers are patterns: names map to their first pattern
    pslots = {n: v for n, v in slots.items()}
    write_c(pal9, spal9, world_where, title_where, pslots, None)
    with open(os.path.join(PROJ, "src", "gfx_data.h"), "a") as fo:
        fo.write(f"\n/* pattern numbers */\n")
        for n, (slot, c, r, ox, oy) in pslots.items():
            fo.write(f"#define PAT_{n.upper()} {slot}   /* {c} x {r} */\n")
        fo.write(f"#define TITLE_COL0 {WORLD_W}   /* the title's columns follow the house's in the index */\n")
        fo.write(f"#define HUD_PAGE {2 * WORLD_BANK + 1}   /* the HUD bar: 320 columns of 16 rows */\n")
    # previews
    lut = np.array(pal + [(0, 0, 0)] * (256 - len(pal)), np.uint8)
    Image.fromarray(lut[np.vstack([hud, w[HUD_H:, :320]])], "RGB").save(os.path.join(PROJ, "build", "room0.png"))
    Image.fromarray(lut[w], "RGB").save(os.path.join(PROJ, "build", "world.png"))
    Image.fromarray(lut[t], "RGB").save(os.path.join(PROJ, "build", "title.png"))
    sheet = np.full((40, 16 * len(slots)), -1, np.int32)
    for i, (name, s) in enumerate(sprites()):
        h = min(40, s.a.shape[0])
        sheet[:h, i * 16:i * 16 + 16] = s.a[:h, :16]
    slut = np.array(spal, np.uint8)
    img = slut[np.clip(sheet, 0, 255)]
    img[sheet < 0] = (40, 40, 60)
    Image.fromarray(img, "RGB").resize((img.shape[1] * 3, 120), Image.NEAREST).save(os.path.join(PROJ, "build", "sprites.png"))
    tot = sum(len(p) for p in pages)
    print(f"world+title RLE {tot} bytes in {len(pages)} pages, banks {WORLD_BANK}..{SPR_BANK - 1}; "
          f"sprites bank {SPR_BANK}, {len(pats) // 256} patterns; L2 colours {len(pal)}")


if __name__ == "__main__":
    main()
