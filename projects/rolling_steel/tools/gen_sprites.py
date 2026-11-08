"""Sprites for the Next (4-bit patterns, a 16-pen palette each): the
original's sphere sprites (render.c's make_sphere: the marble, steel
chasers, blobs, the wipeout's shards and sparks) and the blob shadow.
Writes src/sprite_data.c. Run: python3 tools/gen_sprites.py

port: the original scales one 32 x 32 sphere to any size on the 3DO's cel
engine; here each size it is drawn at is its own 16 x 16 pattern.
"""
import math
from pathlib import Path
import numpy as np
import rsview

HERE = Path(__file__).resolve().parent
SCALE = rsview.Cam().scale                       # px per unit, Q8


def px_radius(r):
    """render.c's sprite half size for radius r (Q12), in pixels"""
    return ((r * SCALE) >> 4) / 65536.0


def disc(look, rad, cx=8.0, cy=8.0):
    """the sphere sprite of a look, rad pixels in radius, centred at (cx, cy)
    of a 16 x 16 cell: 15-bit colours, 0 clear (sampled like Canvas.sprite)"""
    src = rsview.sphere(look)
    out = np.zeros((16, 16), np.int32)
    for j in range(16):
        for i in range(16):
            u = int((i + 0.5 - (cx - rad)) / (2 * rad) * 32)
            v = int((j + 0.5 - (cy - rad)) / (2 * rad) * 32)
            if 0 <= u < 32 and 0 <= v < 32:
                out[j, i] = src[v, u]
    return out


def rgb333(c):
    r, g, b = rsview.rgb15(c)
    return (round(r * 7 / 31) << 6) | (round(g * 7 / 31) << 3) | round(b * 7 / 31)


def palette_of(cells, n=15):
    """at most n 9-bit colours for the 15-bit pixels of some cells, and a
    map from 15-bit colour to pen 1..n (nearest, by k-means on RGB)"""
    cols = sorted({int(c) for cell in cells for c in cell.flat if c})
    nine = sorted({rgb333(c) for c in cols})

    def rgb9(v):
        return np.array([(v >> 6) & 7, (v >> 3) & 7, v & 7], float)
    if len(nine) > n:
        pts = np.array([rgb9(rgb333(c)) for c in cols])
        cent = pts[np.linspace(0, len(pts) - 1, n).astype(int)]
        for _ in range(20):
            lab = np.argmin(((pts[:, None] - cent[None]) ** 2).sum(-1), 1)
            for k in range(n):
                if np.any(lab == k):
                    cent[k] = pts[lab == k].mean(0)
        nine = sorted({(int(round(c[0])) << 6) | (int(round(c[1])) << 3) | int(round(c[2])) for c in cent})
    pens = {}
    for c in cols:
        v = rgb9(rgb333(c))
        pens[c] = 1 + int(np.argmin([((v - rgb9(p)) ** 2).sum() for p in nine]))
    return nine, pens


def pattern(cell, pens):
    """a 16 x 16 cell as a 4-bit pattern (128 bytes, left pixel high nibble)"""
    b = bytearray()
    for j in range(16):
        for i in range(0, 16, 2):
            a, c = int(cell[j, i]), int(cell[j, i + 1])
            b.append((pens.get(a, 0) if a else 0) << 4 | (pens.get(c, 0) if c else 0))
    return bytes(b)


def shadow(rx, ry):
    """render.c's shadow: a disc darkened on the 3DO; here a dithered dark oval"""
    out = np.zeros((16, 16), np.int32)
    for j in range(16):
        for i in range(16):
            dx, dy = (i + 0.5 - 8) / rx, (j + 0.5 - 8) / ry
            if dx * dx + dy * dy < 1 and (i + j) & 1 == 0:
                out[j, i] = 1                    # pen 1: the shadow colour
    return out


def main():
    groups = []                                  # (name, pal index, cells)
    m_r = px_radius(2048)                        # the marble, r = 0.5
    groups.append(("marble", [disc(rsview.SP_MARBLE, m_r)] +
                   [disc(rsview.SP_MARBLE, r) for r in (1.2, 2.0, 2.8, 3.6, 4.4)]))
    groups.append(("steel", [disc(rsview.SP_STEEL, px_radius(2867))]))
    groups.append(("blob", [disc(rsview.SP_BLOB, px_radius(2253))]))
    pats, pals, names = [], [], []
    for name, cells in groups:
        nine, pens = palette_of(cells)
        pals.append(nine)
        first = len(pats)
        pats += [pattern(c, pens) for c in cells]
        names.append((name, first, len(cells), len(pals) - 1))
    # shadows and the spark share a palette: pen 1 shadow, pen 2 spark
    sh = len(pats)
    for r in (2048, 2867):
        hx = px_radius(r * 7 // 8)
        pats.append(pattern(shadow(hx, hx * 2349 / 4096), {1: 1}))
    spark = np.zeros((16, 16), np.int32)
    spark[7:9, 7:9] = 2
    pats.append(pattern(spark, {2: 2}))
    s, sg = rsview.rgb15, (24 << 10) | (16 << 5) | 6     # the spark's colour
    pals.append([0x000, rgb333(sg)])
    names.append(("shadow", sh, 2, len(pals) - 1))
    names.append(("spark", sh + 2, 1, len(pals) - 1))
    out = ["/* made by tools/gen_sprites.py: the original's sphere sprites as 4-bit patterns */",
           '#include "sprite_data.h"', ""]
    out.append(f"const uint8_t spr_patterns[{len(pats)} * 128] = {{")
    for p in pats:
        out.append("    " + ", ".join(f"0x{b:02X}" for b in p) + ",")
    out.append("};")
    out.append(f"const uint16_t spr_palettes[{len(pals)} * 16] = {{")
    for p in pals:
        p = [0] + list(p) + [0] * (15 - len(p))
        out.append("    " + ", ".join(f"0x{v:03X}" for v in p[:16]) + ",")
    out.append("};")
    (HERE.parent / "src" / "sprite_data.c").write_text("\n".join(out) + "\n")
    h = ["/* made by tools/gen_sprites.py */", "#ifndef SPRITE_DATA_H", "#define SPRITE_DATA_H", "#include <stdint.h>",
         f"#define SPR_NPAT {len(pats)}", f"#define SPR_NPAL {len(pals)}"]
    for name, first, n, pal in names:
        h += [f"#define PAT_{name.upper()} {first}", f"#define PAL_{name.upper()} {pal}"]
    h += [f"#define MARBLE_R_PX {m_r:.2f}",
          "extern const uint8_t spr_patterns[];", "extern const uint16_t spr_palettes[];", "#endif", ""]
    (HERE.parent / "src" / "sprite_data.h").write_text("\n".join(h))
    print(f"  {len(pats)} patterns, {len(pals)} palettes")


if __name__ == "__main__":
    main()
