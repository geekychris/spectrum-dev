#!/usr/bin/env python3
"""Orbital Patrol's sprite images for the Next (16 x 16, 8 bits a pixel).

The shapes are the original's (amiga_mcp/examples/orbital_patrol/draw.c
draws everything with RectFill: the same rectangles here, pixel for
pixel), recoloured for 256 colours with some shading. Colours are the
Next's default palette, RRRGGGBB; 0xE3 is transparent.

Output: gfx/spr.bin (the patterns, 32 an 8K page, loaded into 16K bank 21
(pages 42, 43) by the .nex),
src/gfx_data.asm (the BANK section), src/gfx_data.h (pattern numbers).

    python3 tools/gen_gfx.py
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
T = 0xE3                      # transparent


def rgb(r, g, b):
    """12-bit Amiga colour components (0..15) -> RRRGGGBB."""
    v = (round(r * 7 / 15) << 5) | (round(g * 7 / 15) << 2) | round(b * 3 / 15)
    return 0xE2 if v == T else v      # the original's magenta is the transparent index


WHITE, YELLOW, ORANGE, GREEN = rgb(15, 15, 15), rgb(14, 14, 0), rgb(15, 8, 0), rgb(0, 15, 0)
MAGENTA, RED, CYAN = rgb(15, 0, 15), rgb(15, 0, 0), rgb(0, 12, 15)
# shading added for the Next
DK_YELLOW, LT_YELLOW = rgb(10, 9, 0), rgb(15, 15, 8)
DK_GREEN, LT_GREEN = rgb(0, 9, 0), rgb(8, 15, 8)
DK_MAGENTA, DK_RED, DK_CYAN = rgb(9, 0, 9), rgb(9, 0, 0), rgb(0, 7, 10)
GREY, SKIN = rgb(10, 10, 10), rgb(5, 10, 0)     # the human's limbs: COL_HUMAN2 = terrain highlight


def blank():
    return [[T] * 16 for _ in range(16)]


def rect(p, x1, y1, x2, y2, c):
    for y in range(y1, y2 + 1):
        for x in range(x1, x2 + 1):
            if 0 <= x < 16 and 0 <= y < 16:
                p[y][x] = c


PATTERNS = []                 # (name, pixels)


def add(name, p):
    PATTERNS.append((name, p))


# ---- ship (draw_ship), 16 x 7; left facing is the original's own mirror (1 pixel over)
s = blank()
rect(s, 2, 1, 13, 5, YELLOW)                  # main body
rect(s, 2, 1, 13, 1, LT_YELLOW)               # port: top highlight
rect(s, 2, 5, 13, 5, DK_YELLOW)               # port: underside shade
rect(s, 0, 2, 1, 4, YELLOW)                   # rear
rect(s, 14, 2, 15, 3, WHITE)                  # nose
add("SHIP_R", s)
s = blank()
rect(s, 3, 1, 14, 5, YELLOW)
rect(s, 3, 1, 14, 1, LT_YELLOW)
rect(s, 3, 5, 14, 5, DK_YELLOW)
rect(s, 15, 2, 16, 4, YELLOW)                 # (x 16 is off the pattern, as 15..16 in the original)
rect(s, 1, 2, 2, 3, WHITE)
add("SHIP_L", s)
s = blank()
rect(s, 0, 2, 3, 4, ORANGE)                   # thrust flame, 4 x 3 (shown at sx-4 or sx+17)
rect(s, 1, 3, 3, 3, YELLOW)
add("FLAME", s)

# ---- enemies (draw_enemies)
for flash in (0, 1):
    s = blank()
    rect(s, 2, 0, 7, 2, GREEN)                # dome
    rect(s, 3, 0, 6, 0, LT_GREEN)
    rect(s, 0, 3, 9, 5, GREEN)                # body
    rect(s, 0, 5, 9, 5, DK_GREEN)
    rect(s, 2, 6, 7, 7, GREEN)                # legs
    rect(s, 2, 7, 7, 7, DK_GREEN)
    if flash:
        rect(s, 4, 1, 5, 1, WHITE)            # anim_frame & 8
    add("DIVER" if not flash else "DIVER_FLASH", s)
s = blank()
rect(s, 1, 0, 8, 7, MAGENTA)
rect(s, 1, 7, 8, 7, DK_MAGENTA)
rect(s, 3, 2, 3, 2, WHITE)                    # eyes
rect(s, 6, 2, 6, 2, WHITE)
add("STALKER", s)
s = blank()
rect(s, 0, 1, 13, 4, CYAN)
rect(s, 0, 4, 13, 4, DK_CYAN)
rect(s, 1, 0, 12, 0, WHITE)
rect(s, 1, 5, 12, 5, WHITE)
add("CHASER", s)
s = blank()
rect(s, 1, 0, 10, 6, RED)
rect(s, 1, 6, 10, 6, DK_RED)
rect(s, 3, 2, 8, 4, ORANGE)
add("DROPPER", s)
s = blank()
rect(s, 2, 0, 5, 0, MAGENTA)
rect(s, 1, 1, 6, 5, MAGENTA)
rect(s, 2, 6, 5, 6, DK_MAGENTA)
rect(s, 2, 2, 3, 3, rgb(15, 8, 15))           # port: a highlight
add("HIVE", s)
s = blank()
rect(s, 0, 0, 3, 3, ORANGE)
rect(s, 1, 1, 2, 2, YELLOW)
add("DRONE", s)

# ---- human (draw_humans), 4 x 6
s = blank()
rect(s, 1, 0, 2, 0, WHITE)                    # head
rect(s, 1, 1, 2, 3, WHITE)                    # body
rect(s, 0, 2, 0, 2, SKIN)                     # arms
rect(s, 3, 2, 3, 2, SKIN)
rect(s, 0, 4, 0, 5, SKIN)                     # legs
rect(s, 3, 4, 3, 5, SKIN)
add("HUMAN", s)

# ---- shots (draw_bullets)
s = blank()
rect(s, 0, 0, 7, 1, WHITE)                    # laser 8 x 2
rect(s, 0, 1, 7, 1, rgb(10, 15, 15))
add("LASER", s)
s = blank()
rect(s, 0, 0, 3, 3, ORANGE)                   # enemy shot 4 x 4
add("EBULLET", s)
s = blank()
rect(s, 0, 0, 4, 4, RED)                      # mine 5 x 5
rect(s, 1, 1, 3, 3, YELLOW)
add("MINE", s)

# ---- particles (draw_particles): 2 x 2 in COL_EXPL_RED + 0..2 = red, cyan and
# colour 12, which is black in the original's palette (those don't show)
for name, c in (("PART_RED", RED), ("PART_CYAN", CYAN)):
    s = blank()
    rect(s, 0, 0, 1, 1, c)
    add(name, s)

# ---- explosions (draw_explosions): radius 2..17, life 15..0: a cross 2r + 1
# across, in yellow (life > 10), orange (> 5) or red; while life > 8 a yellow
# square r/2 around the centre. Drawn as four sprites, each this
# bottom-right quarter mirrored: port: the cross's lines are 2 pixels wide.
for r in range(2, 18):
    life = 15 - (r - 2)
    ring = YELLOW if life > 10 else ORANGE if life > 5 else RED
    s = blank()
    rr = min(r, 15)
    rect(s, 0, 0, rr, 0, ring)
    rect(s, 0, 0, 0, rr, ring)
    if life > 8:
        rect(s, 0, 0, r // 2, r // 2, YELLOW)
    add(f"EXPL_{r}", s)

assert len(PATTERNS) <= 48, "patterns 48..63 are the sprite text's"
data = bytearray()
for _, p in PATTERNS:
    for row in p:
        data += bytes(row)
os.makedirs(os.path.join(PROJ, "gfx"), exist_ok=True)
with open(os.path.join(PROJ, "gfx", "spr.bin"), "wb") as f:
    f.write(data + bytes(-len(data) % 8192))
with open(os.path.join(PROJ, "src", "gfx_data.asm"), "w") as f:
    f.write("; generated by tools/gen_gfx.py - do not edit: the sprite patterns,\n"
            "; into 8K page 42 (16K bank 21) by the .nex\n\n"
            "SECTION BANK_21\nBINARY \"gfx/spr.bin\"\n")
with open(os.path.join(PROJ, "src", "gfx_data.h"), "w") as f:
    f.write("/* generated by tools/gen_gfx.py - do not edit */\n#ifndef GFX_DATA_H\n#define GFX_DATA_H\n\n")
    f.write("#define SPR_PAGE 42                  /* the patterns, 32 a page */\n")
    f.write(f"#define SPR_COUNT {len(PATTERNS)}\n")
    for i, (name, _) in enumerate(PATTERNS):
        f.write(f"#define P_{name} {i}\n")
    f.write("#define P_EXPL(r) (P_EXPL_2 + (r) - 2)\n#define P_TEXT 48                    /* 48..63: sprite text */\n\n#endif\n")
print(len(PATTERNS), "patterns")
