#!/usr/bin/env python3
"""Planet Chomp's pictures for the Next.

  gfx/stars_N.bin   the starfield (the original's render_cel.c: 90 stars on a
                    160 x 120 texture drawn at 2x, from its own random
                    numbers), 256 x 192 Layer 2 bytes in three 16K banks;
                    each frame starts as a DMA copy of it
  gfx/spr_N.bin     the sprites: the original's own textures (its sprites.c,
                    run by tools/gen_sprites.c -> build/textures.bin), each
                    at SIZES pixels across, centred in one 16 x 16 pattern
                    (up to 16) or four (a 32 x 32 quad); a texture's sizes
                    one after another, SPR_REC bytes a texture, from page
                    SPR_PAGE (16K banks)
  gfx/extra.bin     patterns loaded once (bank 25): the score popups (200 400 800 1600
                    in the 5 x 7 font, two patterns each), the radar's dots,
                    the lives icon
  src/gfx_data.asm  those banks for the .nex
  src/spr_data.c/.h the sprite palette, the sizes, where things are
  src/hud_tiles.h   the radar's rings and the power bar as tiles

    python3 tools/gen_gfx.py      (after build/gen_sprites)
"""
import re
from pathlib import Path

import numpy as np
from PIL import Image
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "sdk" / "tools"))
import nextpic  # noqa: E402

HERE = Path(__file__).resolve().parent.parent
C_SPACE, C_STAR1 = 0, 1
STARS_BANK = 22
SPR_BANK = 26
EXTRA_BANK = 25
SIZES = [4, 6, 8, 10, 12, 14, 16, 20, 24, 28]
N_SMALL = sum(1 for d in SIZES if d <= 16)
SPR_REC = N_SMALL * 256 + (len(SIZES) - N_SMALL) * 1024
T = 0xE3
# the radar's dots (the original's pens): GLOOM SPARK MIST MOSS, frightened,
# eaten (eyes), a key (gold), the nest (dim), the chomper (hero)
DOTS = [(0xAA, 0x50, 0xFF), (0xF0, 0xDC, 0x3C), (0xD2, 0xD2, 0xE6), (0x28, 0xC8, 0xAA),
        (0x50, 0x80, 0xFF), (0x88, 0x90, 0xA8), (0xFF, 0xD8, 0x38), (0x98, 0xA0, 0xC0),
        (0x8C, 0xE6, 0x3C)]
POPUPS = ["200", "400", "800", "1600"]


def stars():
    """the original's sky texture: colour index per texel"""
    w, h = 160, 120
    tex = [[C_SPACE] * w for _ in range(h)]
    r = 7
    for _ in range(90):
        r = (r * 1103515245 + 12345) & 0xFFFFFFFF
        x = (r >> 16) % w
        r = (r * 1103515245 + 12345) & 0xFFFFFFFF
        y = (r >> 16) % h
        r = (r * 1103515245 + 12345) & 0xFFFFFFFF
        b = 110 + (r >> 16) % 146
        tex[y][x] = C_STAR1 + min(3, (b - 110) * 4 // 146)
    # 160 x 120 at 2x on the 320 x 240 frame = 256 x 192 here (x 1.6, nearest),
    # drawn 96 rows high (the copper shows each row twice: main.c)
    out = bytearray(256 * 192)
    for y in range(96):
        for x in range(256):
            out[y * 256 + x] = tex[y * 120 // 96][x * 160 // 256]
    return out


def textures():
    """build/textures.bin: 32 x 32 RGBA arrays"""
    raw = np.frombuffer((HERE / "build" / "textures.bin").read_bytes(), "<u2").reshape(-1, 32, 32)
    out = []
    for t in raw:
        r = ((t >> 10) & 31) * 255 // 31
        g = ((t >> 5) & 31) * 255 // 31
        b = (t & 31) * 255 // 31
        a = np.where(t != 0, 255, 0)
        out.append(np.stack([r, g, b, a], -1).astype(np.uint8))
    return out


def scaled(tex, d):
    """a texture d pixels across (box filter on premultiplied colour), RGBA"""
    img = Image.fromarray(tex, "RGBA")
    pre = np.asarray(img).astype(np.float64)
    pre[..., :3] *= pre[..., 3:] / 255
    small = np.asarray(Image.fromarray(pre.astype(np.uint8), "RGBA").resize((d, d), Image.BOX)).astype(np.float64)
    a = small[..., 3:]
    rgb = np.where(a > 0, small[..., :3] * 255 / np.maximum(a, 1), 0)
    return np.concatenate([np.clip(rgb, 0, 255), np.where(a >= 100, 255, 0)], -1).astype(np.uint8)


def font5():
    src = (HERE.parents[1] / "sdk" / "src" / "font5.c").read_text()
    rows = re.findall(r"\{ (0x[0-9A-F]{2}(?:, 0x[0-9A-F]{2}){6}) \}", src)
    return [[int(v, 16) for v in r.split(", ")] for r in rows]           # '!' .. 'Z'


def text_rgba(s, colour):
    """s in the 5 x 7 font with a 1-pixel dark outline, 32 x 16"""
    f = font5()
    img = np.zeros((16, 32, 4), np.uint8)
    w = len(s) * 6 - 1
    x0 = (32 - w) // 2
    on = np.zeros((16, 32), bool)
    for k, ch in enumerate(s):
        g = f[ord(ch) - 33]
        for y in range(7):
            for x in range(5):
                if g[y] & (0x80 >> x):
                    on[4 + y, x0 + k * 6 + x] = True
    edge = np.zeros_like(on)
    for dy in (-1, 0, 1):
        for dx in (-1, 0, 1):
            edge |= np.roll(np.roll(on, dy, 0), dx, 1)
    img[edge] = (16, 24, 40, 255)
    img[on] = (*colour, 255)
    return img


def dot_rgba(colour, r):
    img = np.zeros((16, 16, 4), np.uint8)
    for y in range(16):
        for x in range(16):
            if (x - 7) ** 2 + (y - 7) ** 2 <= r * r:
                img[y, x] = (*colour, 255)
    return img


def sprites():
    texs = textures()
    small = {}                       # (texture, size) -> RGBA d x d
    for t, tex in enumerate(texs):
        for d in SIZES:
            small[t, d] = scaled(tex, d)
    extras = [text_rgba(s, (255, 255, 255)) for s in POPUPS]
    dots = [dot_rgba(c, 1) for c in DOTS]
    life = dot_rgba((0x8C, 0xE6, 0x3C), 5)
    # one palette for everything (index 0xE3 is transparent)
    pix = [im[im[..., 3] > 0][:, :3] for im in list(small.values()) + extras + dots + [life]]
    pal = nextpic.quantize(np.concatenate(pix), 255)
    pal = [tuple(int(v) for v in c) for c in pal]
    def idx(im):
        out = nextpic.nearest(im[..., :3], np.array(pal))
        out = np.where(out >= T, out + 1, out)         # skip 0xE3
        return np.where(im[..., 3] > 0, out, T).astype(np.uint8)
    blob = bytearray()
    for t in range(len(texs)):
        for d in SIZES:
            n = 16 if d <= 16 else 32
            cell = np.full((n, n), T, np.uint8)
            o = (n - d) // 2
            cell[o:o + d, o:o + d] = idx(small[t, d])
            if n == 16:
                blob += bytes(cell.flatten())
            else:
                for p in nextpic.quad_patterns(list(cell.flatten())):
                    blob += bytes(p)
    assert len(blob) == len(texs) * SPR_REC
    extra = bytearray()
    for im in extras:                                   # two patterns side by side
        cell = idx(im)
        extra += bytes(cell[:, :16].flatten()) + bytes(cell[:, 16:].flatten())
    for im in dots + [life]:
        extra += bytes(idx(im).flatten())
    pal9 = [nextpic.rgb9_from_rgb(*c) for c in pal]
    pal9 = (pal9[:T] + [0] + pal9[T:] + [0] * 256)[:256]
    return blob, extra, pal9, len(texs)


def hud_tiles():
    """the radar's rings (the original's ring(): 32 segments, radius 34 in pen
    3 and 17 in pen 4, centre 282, 214 on the 320 x 256 frame) as 4-bit
    tiles: (definitions, [(col, row, tile)]); the power bar: 8 tiles, 0..7
    pixels filled (pen 5 on pen 6), then a full one"""
    from PIL import ImageDraw
    import math
    img = Image.new("P", (320, 256), 0)
    d = ImageDraw.Draw(img)
    for r, pen in ((34, 3), (17, 4)):
        pts = [(282 + int(r * math.cos(k * math.pi / 16)), 214 - int(r * math.sin(k * math.pi / 16))) for k in range(33)]
        d.line(pts, fill=pen)
    px = np.asarray(img)
    defs, place = [], []
    for row in range(32):
        for col in range(40):
            t = px[row * 8:row * 8 + 8, col * 8:col * 8 + 8]
            if not t.any():
                continue
            b = bytes((int(t[y, x]) << 4) | int(t[y, x + 1]) for y in range(8) for x in range(0, 8, 2))
            if b not in defs:
                defs.append(b)
            place.append((col, row, defs.index(b)))
    bar = []
    for n in range(9):
        t = np.full((8, 8), 6, np.uint8)
        t[:, :n] = 5
        t[0, :] = 0
        t[6:, :] = 0
        bar.append(bytes((int(t[y, x]) << 4) | int(t[y, x + 1]) for y in range(8) for x in range(0, 8, 2)))
    return defs, place, bar


def main():
    gfx = HERE / "gfx"
    gfx.mkdir(exist_ok=True)
    data = stars()
    asm = ["; made by tools/gen_gfx.py - do not edit: the starfield (three 16K banks,",
           "; l2_copy_to_back copies them into the back buffer each frame), the sprites", ""]
    for i in range(3):
        name = f"stars_{i}.bin"
        (gfx / name).write_bytes(data[i * 16384:(i + 1) * 16384])
        asm += [f"SECTION BANK_{STARS_BANK + i}", f'BINARY "gfx/{name}"']
    blob, extra, pal9, ntex = sprites()
    nb = (len(blob) + 16383) // 16384
    for i in range(nb):
        name = f"spr_{i}.bin"
        (gfx / name).write_bytes(blob[i * 16384:(i + 1) * 16384])
        asm += [f"SECTION BANK_{SPR_BANK + i}", f'BINARY "gfx/{name}"']
    (gfx / "extra.bin").write_bytes(extra)
    asm += [f"SECTION BANK_{EXTRA_BANK}", 'BINARY "gfx/extra.bin"']
    (HERE / "src" / "gfx_data.asm").write_text("\n".join(asm) + "\n")
    h = ["/* made by tools/gen_gfx.py: do not edit */", "#ifndef SPR_DATA_H", "#define SPR_DATA_H",
         "#include <stdint.h>",
         f"#define SPR_PAGE {SPR_BANK * 2}               /* the sprites' first 8K page */",
         f"#define SPR_REC {SPR_REC}                /* bytes a texture: its sizes */",
         f"#define SPR_SIZES {len(SIZES)}", f"#define SPR_SMALL {N_SMALL}                 /* sizes in one pattern */",
         f"#define SPR_TEXTURES {ntex}",
         f"#define SPR_EXTRA_N {len(extra) // 256}             /* patterns from the start of page SPR_EXTRA_PAGE: */",
         f"#define SPR_EXTRA_PAGE {EXTRA_BANK * 2}         /* popups 200 400 800 1600 (2 each), dots, life */",
         "extern const uint8_t spr_size[SPR_SIZES];      /* pixels across */",
         "extern const uint16_t spr_pal[256];             /* 9-bit */",
         "#endif"]
    (HERE / "src" / "spr_data.h").write_text("\n".join(h) + "\n")
    c = ["/* made by tools/gen_gfx.py: do not edit */", '#include "spr_data.h"', "",
         "const uint8_t spr_size[SPR_SIZES] = { " + ", ".join(map(str, SIZES)) + " };",
         "const uint16_t spr_pal[256] = {"]
    for i in range(0, 256, 16):
        c.append("    " + ", ".join(str(v) for v in pal9[i:i + 16]) + ",")
    c.append("};")
    (HERE / "src" / "spr_data.c").write_text("\n".join(c) + "\n")
    defs, place, bar = hud_tiles()
    c = ["/* made by tools/gen_gfx.py: do not edit - the HUD's tiles (included by hud.c) */", "",
         f"#define RING_TILES {len(defs)}", f"#define RING_CELLS {len(place)}",
         "/* the radar's rings: tile definitions, then (col, row, tile) cells */",
         "static const uint8_t ring_def[RING_TILES * 32] = {"]
    for b in defs:
        c.append("    " + ", ".join(str(v) for v in b) + ",")
    c += ["};", "static const uint8_t ring_cell[RING_CELLS * 3] = {"]
    for col, row, t in place:
        c.append(f"    {col}, {row}, {t},")
    c += ["};", "/* the power bar: 0..8 pixels filled */", "static const uint8_t bar_def[9 * 32] = {"]
    for b in bar:
        c.append("    " + ", ".join(str(v) for v in b) + ",")
    c.append("};")
    (HERE / "src" / "hud_tiles.h").write_text("\n".join(c) + "\n")
    print(f"gen_gfx: starfield in banks {STARS_BANK}-{STARS_BANK + 2}, {ntex} sprites x {len(SIZES)} sizes "
          f"in banks {SPR_BANK}-{SPR_BANK + nb - 1}, {len(extra) // 256} extra patterns")


if __name__ == "__main__":
    main()
