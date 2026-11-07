#!/usr/bin/env python3
"""Lunar Rider's pictures for the Spectrum Next, made from the original's
shapes (amiga_mcp/examples/lunar_rider/draw.c), redrawn with shading in the
Next's 256 colours.

  sprites   the buggy (two wheel frames), the small and the large UFO (two
            flash frames each), the meteor, the bomb, the two bullets and
            the explosion: one quarter of the original's square explosion a
            radius and colour stage (shown four times, mirrored). 16 x 16,
            8 bpp (RRRGGGBB, 0xE3 transparent) -> gfx/spr.bin (bank 21)
  tilemap   the sky (a gradient) and the far mountains, 8 x 8 tiles at 4 bpp
            and the 40-column map (320 pixels, two periods of the far
            mountain profile) -> src/gfx_data.c
  heights   the near mountains' profile, 512 columns -> src/gfx_data.c

The mountain profiles are the original's: approx_sin() (a triangle wave),
far h = 140 - sin(x * 3/2) * 40/255 clamped to 90..140 (filled to row 160;
port: x * 8/5, a 160-pixel period),
near h = 130 - sin(x * 5/2) * 60/255 clamped to 80..160 (filled to 170).
port: one height a pixel column (the original steps 2 pixels).

    python3 tools/gen_gfx.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
T = 0xE3                                    # transparent


def rgb8(r, g, b):
    """0..255 components -> RRRGGGBB"""
    return (min(7, round(r * 7 / 255)) << 5) | (min(7, round(g * 7 / 255)) << 2) | min(3, round(b * 3 / 255))


def amiga(c):
    """the original's 12-bit palette entry -> RRRGGGBB"""
    r, g, b = (c >> 8) & 15, (c >> 4) & 15, c & 15
    v = rgb8(r * 17, g * 17, b * 17)
    return v if v != T else v ^ 1


def shade(c, k):
    """c (12-bit Amiga colour) times k, as RRRGGGBB"""
    r, g, b = (c >> 8) & 15, (c >> 4) & 15, c & 15
    v = rgb8(min(255, r * 17 * k), min(255, g * 17 * k), min(255, b * 17 * k))
    return v if v != T else v ^ 1


# the original's palette
WHITE, YELLOW, GRAY, RED, ORANGE, CYAN = 0xFFF, 0xFD0, 0xAAA, 0xF00, 0xF80, 0x0EF
GREEN, BLUE, BRYELLOW, DKGRAY, PINK = 0x0F0, 0x44F, 0xFF0, 0x555, 0xF8F


def approx_sin(phase):
    phase &= 255
    if phase < 64:
        return phase * 4
    if phase < 192:
        return 255 - (phase - 64) * 2
    return -255 + (phase - 192) * 4


def cdiv(a, b):
    return int(a / b)


def far_h(u):
    # port: a 160-pixel period (the original's u * 3/2 phase gives ~171), so
    # the far mountains repeat every 320 pixels, the tilemap's width: they
    # then scroll by the tilemap's scroll register alone
    return max(90, min(140, 120 + 20 - cdiv(approx_sin(u * 8 // 5) * 40, 255)))


def near_h(u):
    return max(80, min(160, 100 + 30 - cdiv(approx_sin(u * 5 // 2) * 60, 255)))


# ---------------------------------------------------------------- sprites

class Img:
    def __init__(self, w=16, h=16):
        self.w, self.h = w, h
        self.p = [[T] * w for _ in range(h)]

    def rect(self, x0, y0, x1, y1, c):
        for y in range(max(0, y0), min(self.h, y1 + 1)):
            for x in range(max(0, x0), min(self.w, x1 + 1)):
                self.p[y][x] = c

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.p[y][x] = c

    def pattern(self, px, py):
        """the 16 x 16 pattern at (px, py)"""
        out = bytearray()
        for y in range(16):
            for x in range(16):
                yy, xx = py + y, px + x
                out.append(self.p[yy][xx] if yy < self.h and xx < self.w else T)
        return bytes(out)


def buggy(frame):
    """draw_buggy(): the buggy's top-left (bx, by) is pixel (0, 5) here: the
    up gun reaches by - 5; 32 x 16 = two patterns"""
    im = Img(32, 16)
    o = 5
    hi, mid, lo = shade(YELLOW, 1.15), amiga(YELLOW), shade(YELLOW, 0.65)
    im.rect(2, o + 1, 21, o + 5, mid)            # body
    im.rect(2, o + 1, 21, o + 1, hi)
    im.rect(2, o + 5, 21, o + 5, lo)
    im.rect(6, o - 2, 17, o, mid)                # roof
    im.rect(6, o - 2, 17, o - 2, hi)
    im.rect(16, o - 1, 18, o, amiga(CYAN))       # windshield
    im.put(16, o - 1, amiga(WHITE))
    im.rect(22, o + 2, 24, o + 3, amiga(ORANGE))  # forward gun
    im.rect(11, o - 5, 12, o - 2, amiga(ORANGE))  # up gun
    im.rect(7, o + 6, 16, o + 7, amiga(DKGRAY))   # undercarriage
    for wx in (2, 17):                           # wheels, 5 x 5 with a hub
        im.rect(wx, o + 6, wx + 4, o + 10, amiga(GRAY))
        im.put(wx, o + 6, T)
        im.put(wx + 4, o + 6, T)
        im.put(wx, o + 10, T)
        im.put(wx + 4, o + 10, T)
        if frame:                                # spokes: the original's two frames
            im.rect(wx + 2, o + 7, wx + 2, o + 9, amiga(DKGRAY))
        else:
            im.rect(wx + 1, o + 8, wx + 3, o + 8, amiga(DKGRAY))
    return im


def ufo_small(flash):
    im = Img()
    body = amiga(WHITE) if flash else amiga(CYAN)
    im.rect(2, 2, 9, 5, body)
    im.rect(2, 2, 9, 2, shade(WHITE if flash else CYAN, 1.2))
    im.rect(4, 0, 7, 1, amiga(CYAN))             # dome
    im.put(5, 0, amiga(WHITE))
    im.rect(0, 4, 11, 6, amiga(DKGRAY))           # bottom
    im.rect(1, 6, 10, 6, shade(DKGRAY, 0.6))
    return im


def ufo_large(flash):
    im = Img(32, 16)
    body = amiga(WHITE) if flash else amiga(CYAN)
    im.rect(3, 3, 16, 8, body)
    im.rect(3, 3, 16, 3, shade(WHITE if flash else CYAN, 1.2))
    im.rect(6, 0, 13, 2, amiga(CYAN))            # dome
    im.rect(8, 0, 9, 0, amiga(WHITE))
    im.rect(0, 7, 19, 10, amiga(DKGRAY))          # bottom
    im.rect(1, 10, 18, 10, shade(DKGRAY, 0.6))
    im.rect(2, 9, 3, 10, amiga(RED))             # lights
    im.rect(16, 9, 17, 10, amiga(RED))
    if flash:
        im.put(2, 9, amiga(BRYELLOW))
        im.put(16, 9, amiga(BRYELLOW))
    return im


def meteor():
    """drawn from (x, y - 2): the trail rises above the meteor's top"""
    im = Img()
    o = 2
    im.rect(1, o + 1, 6, o + 6, amiga(PINK))
    im.rect(2, o + 2, 5, o + 5, amiga(ORANGE))
    im.rect(3, o + 3, 4, o + 4, amiga(BRYELLOW))
    im.rect(7, o - 2, 8, o, amiga(RED))          # trail
    im.put(8, o - 2, shade(RED, 0.6))
    return im


def bomb():
    """drawn from (x - 1, y - 1)"""
    im = Img()
    im.rect(0, 0, 3, 3, amiga(BLUE))
    im.put(0, 0, T)
    im.put(3, 3, shade(BLUE, 0.6))
    im.put(1, 1, amiga(WHITE))
    return im


def bullet_fwd():
    im = Img()
    im.rect(0, 0, 3, 1, amiga(ORANGE))
    im.rect(3, 0, 3, 1, amiga(BRYELLOW))
    return im


def bullet_up():
    im = Img()
    im.rect(0, 0, 1, 3, amiga(ORANGE))
    im.rect(0, 0, 1, 0, amiga(BRYELLOW))
    return im


# explosion stages: (radius, life) as the original's update_explosions()
# produces them on screen: spawned at radius 2, life 12; while life > 6 the
# radius grows by 2. Outer ring white while life > 6 else orange; core (r > 3)
# bright yellow while life > 8 else red.
EXPL = [(2, 12), (4, 11), (6, 10), (8, 9), (10, 8), (12, 7), (12, 6)]


def expl_quarter(r, life):
    """the top-left quarter: the square's centre is pixel (15, 15)"""
    im = Img()
    outer = amiga(WHITE) if life > 6 else amiga(ORANGE)
    im.rect(15 - r, 15 - r, 15, 15, outer)
    if r > 3:
        core = amiga(BRYELLOW) if life > 8 else amiga(RED)
        im.rect(15 - r // 2, 15 - r // 2, 15, 15, core)
    return im


def sprites():
    pats, names = [], {}

    def add(name, im, cols=1):
        names[name] = len(pats)
        for c in range(cols):
            pats.append(im.pattern(c * 16, 0))

    add("BUGGY0", buggy(0), 2)
    add("BUGGY1", buggy(1), 2)
    add("UFOS0", ufo_small(0))
    add("UFOS1", ufo_small(1))
    add("UFOL0", ufo_large(0), 2)
    add("UFOL1", ufo_large(1), 2)
    add("METEOR", meteor())
    add("BOMB", bomb())
    add("BULLET_F", bullet_fwd())
    add("BULLET_U", bullet_up())
    names["EXPL"] = len(pats)
    for r, life in EXPL:
        pats.append(expl_quarter(r, life).pattern(0, 0))
    return pats, names


# ---------------------------------------------------------------- tilemap

SKY_ROWS = 25                     # tile rows 0..24: rows 0..199
# tilemap pens: 0 transparent, 1..6 the sky gradient, 7 far rim, 8..11 far body
SKY = [(0, 0, 0), (0, 0, 4), (0, 0, 10), (4, 0, 16), (8, 2, 22), (12, 4, 30)]
FAR_RIM = (110, 60, 150)
FAR_BODY = [(64, 32, 96), (56, 28, 84), (46, 24, 72), (38, 20, 60)]   # 0x214 shaded


def sky_pen(y):
    return 1 + min(5, (y // 8) * 6 // SKY_ROWS)     # changes on tile rows only (fewer tiles)


def far_pen(u, y):
    h = far_h(u)
    if y < h or y > 160:
        return sky_pen(y) if y < 200 else 1     # below the sky: black, opaque
    if y - h < 2:
        return 7
    return 8 + max(0, min(3, ((y // 8) * 8 - 96) // 16))   # by tile row (fewer tiles)


def tiles():
    defs, index = [], {}
    tmap = []                     # 40 columns x 25 rows
    for tc in range(40):
        col = []
        for tr in range(SKY_ROWS):
            t = bytearray()
            for yy in range(8):
                y = tr * 8 + yy
                row = [far_pen(tc * 8 + xx, y) for xx in range(8)]
                for k in range(0, 8, 2):
                    t.append(row[k] << 4 | row[k + 1])
            t = bytes(t)
            if t not in index:
                index[t] = len(defs)
                defs.append(t)
            col.append(index[t])
        tmap.append(col)
    return defs, tmap


def rgb9(c):
    r, g, b = c
    return (round(r * 7 / 255) << 6) | (round(g * 7 / 255) << 3) | round(b * 7 / 255)


# ---------------------------------------------------------------- write

def main():
    pats, names = sprites()
    defs, tmap = tiles()
    assert len(pats) <= 64, len(pats)
    assert len(defs) <= 170, len(defs)
    os.makedirs(os.path.join(PROJ, "gfx"), exist_ok=True)
    blob = b"".join(pats)
    with open(os.path.join(PROJ, "gfx", "spr.bin"), "wb") as f:
        f.write(blob)
    with open(os.path.join(PROJ, "src", "gfx_data.asm"), "w") as f:
        f.write("; generated by tools/gen_gfx.py - do not edit: the sprite patterns,\n"
                "; straight into Next bank 21 (8K pages 42-43) by the .nex\n\n"
                "SECTION BANK_21\nBINARY \"gfx/spr.bin\"\n")
    pens = [(0, 0, 0)] + SKY + [FAR_RIM] + FAR_BODY
    with open(os.path.join(PROJ, "src", "gfx_data.h"), "w") as f:
        f.write("/* generated by tools/gen_gfx.py - do not edit */\n#ifndef GFX_DATA_H\n#define GFX_DATA_H\n"
                "#include <stdint.h>\n\n#define SPR_PAGE 42                  /* bank 21 */\n"
                f"#define SPR_COUNT {len(pats)}\n")
        for k, v in names.items():
            f.write(f"#define P_{k} {v}\n")
        f.write(f"#define EXPL_STAGES {len(EXPL)}\n")
        f.write(f"#define TILE_DEFS {len(defs)}\n#define SKY_ROWS {SKY_ROWS}\n#define TM_PENS {len(pens)}\n\n")
        f.write("extern const uint8_t tile_defs[TILE_DEFS * 32];\n"
                "extern const uint8_t far_map[40][SKY_ROWS];  /* the tilemap's 40 columns, rows 0..SKY_ROWS-1 */\n"
                "extern const uint16_t tm_pens[TM_PENS];        /* RGB333 */\n"
                "extern const uint8_t near_h[512];              /* near mountain top row per x mod 512 */\n"
                "extern const uint8_t expl_r[EXPL_STAGES];      /* stage -> radius */\n"
                "#endif\n")
    with open(os.path.join(PROJ, "src", "gfx_data.c"), "w") as f:
        f.write("/* generated by tools/gen_gfx.py - do not edit */\n#include \"gfx_data.h\"\n\n")
        f.write("const uint8_t tile_defs[TILE_DEFS * 32] = {\n")
        for t in defs:
            f.write("    " + ", ".join(f"0x{b:02X}" for b in t) + ",\n")
        f.write("};\n\nconst uint8_t far_map[40][SKY_ROWS] = {\n")
        for col in tmap:
            f.write("    {" + ", ".join(str(v) for v in col) + "},\n")
        f.write("};\n\nconst uint16_t tm_pens[TM_PENS] = {" + ", ".join(str(rgb9(p)) for p in pens) + "};\n\n")
        f.write("const uint8_t near_h[512] = {\n")
        hs = [near_h(u) for u in range(512)]
        for i in range(0, 512, 16):
            f.write("    " + ", ".join(str(v) for v in hs[i:i + 16]) + ",\n")
        f.write("};\n\nconst uint8_t expl_r[EXPL_STAGES] = {" + ", ".join(str(r) for r, _ in EXPL) + "};\n")
    print(f"{len(pats)} sprite patterns, {len(defs)} tiles")


if __name__ == "__main__":
    main()
