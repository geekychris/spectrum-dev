#!/usr/bin/env python3
"""Stack Tower's pictures for the Spectrum Next, drawn on the host with the
original's own geometry (tools/geom.py: render.c's camera, tower and faces).

The Next shows the tower at the original's 320 x 256 (Layer 2's 320 mode,
the 3D view in columns 0..199, the panel at 200..319). The view turns in
steps of an eighth of a column (port: the original turns smoothly), so
everything that depends on the view is drawn here for those steps:

  base pictures   for each of the 8 steps within a column: the painted
                  backdrop, the core with its grid and the red line, columns
                  32..159 (four 8K pages; the tower never leaves them). The
                  game copies one into the back buffer by DMA each frame.
  brick images    a block at each of the 41 positions from the front to 5
                  columns round (the left half is the right half mirrored)
                  and each of the 24 rows, as l2wrun images (pen offsets:
                  lit 0, shaded 1), in three sets: board blocks, the falling piece (which
                  stands out further), the ghost

The core's 16 panels each have their own pen (all the core's colour): the
drop guide is the copper changing those of the piece's columns between the
guide's first and last lines (tower.c).

Outputs (checked in):
  gfx/scr_<k>.bin     the first screen (backdrop, core, panel), banks 16..20
  gfx/base_<k>.bin    the base pictures, banks BASE_BANK..
  gfx/brk_<k>.bin     the brick images, banks BRICK_BANK..
  gfx/title_<k>.bin   the title painting (its own palette), banks TITLE_BANK..
  gfx/pal_0.bin       the palettes, bank PAL_BANK
  src/gfx_data.asm    the BANK_n sections
  src/gfx_data.c, .h  palettes, where each brick block is

    python3 tools/gen_gfx.py
"""
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "..", "..", "sdk", "tools"))
sys.path.insert(0, HERE)
from nextpic import avoid_transparent, from333, nearest, quantize, rgb9, to333  # noqa: E402
from runimg import encode  # noqa: E402
import geom  # noqa: E402

BRICK_WINDOW = 0x0000           # the brick pages are mapped into MMU slot 0 to draw
STEPS = 8                       # view steps a column
NANG = 5 * STEPS + 1            # brick positions 0 .. 5 columns round
SCR_BANK, BACK_BANK = 16, 21    # the two Layer 2 screens
BASE_BANK = 26                  # 8 x 4 pages
BASE_X0 = 32                    # base pictures: columns 32..159
CACHE_BANK = BASE_BANK + 16     # 2 banks, filled by the game (tower.c)
PAL_BANK = CACHE_BANK + 2       # the palettes and the guide's rows
MUSIC_BANK = PAL_BANK + 1       # tools/make_music.py's
BRICK_BANK = MUSIC_BANK + 1

# render.c's palette (12-bit Amiga RGB): pens 0..31
STACK_PAL = [0x000, 0xFFF, 0x223, 0x99A, 0x113, 0x334, 0x557, 0xE22,
             0xE44, 0x922, 0xF92, 0xA51, 0xFE4, 0xA92, 0x5D4, 0x283,
             0x4CE, 0x278, 0x46E, 0x239, 0xB5E, 0x628, 0xE6A, 0x935,
             0x888, 0x555, 0x446, 0xFE2, 0x2EF, 0x3F5, 0xF82, 0x111]
PEN_PANEL, PEN_CORE, PEN_GUIDE, PEN_LINE = 2, 5, 6, 7
PEN_SLOT = 32                   # the core's 16 panels each have a pen (the copper's drop guide)
PEN_GHOST = 48                  # per piece: the core's colour, then the piece's lit colour
FIRST_BACKDROP_PEN = 64

SETS = [("board", lambda rel, row: geom.brick(rel, row, geom.BRICK)),
        ("piece", lambda rel, row: geom.brick(rel, row, geom.BRICK + 6)),
        ("ghost", lambda rel, row: geom.brick(rel, row, 6))]


def c12_rgb(c):
    return ((c >> 8 & 15) * 17, (c >> 4 & 15) * 17, (c & 15) * 17)


def picture_palette(rgb, n, first):
    """quantize an RGB picture to n colours: (indices + first, 9-bit palette entries)"""
    flat = rgb.reshape(-1, 3)
    pal = quantize(flat, n)
    idx = nearest(rgb, pal)
    return idx + first, [int(c[0]) << 6 | int(c[1]) << 3 | int(c[2]) for c in to333(pal)]


def backdrop():
    im = np.asarray(Image.open(os.path.join(ROOT, "assets", "backdrop_200x256.png")).convert("RGB"))
    idx, pal = picture_palette(im, 256 - FIRST_BACKDROP_PEN, FIRST_BACKDROP_PEN)
    return idx.astype(np.uint8), pal


def draw_core(view, frac):
    pens = {"grid": PEN_PANEL, "line": PEN_LINE}
    for poly, kind in geom.core_faces(frac):
        geom.raster(view, poly, PEN_SLOT + kind[1] if isinstance(kind, tuple) else pens[kind])


def column_major(img):
    """rows x columns -> Layer 2 320 mode bytes (x * 256 + y)"""
    return np.ascontiguousarray(img.T).tobytes()


def bank_asm(prefix, data, bank):
    files, asm = [], []
    for k in range(0, len(data), 16384):
        name = f"gfx/{prefix}_{k // 16384}.bin"
        chunk = data[k:k + 16384]
        open(os.path.join(ROOT, name), "wb").write(chunk)
        asm += [f"SECTION BANK_{bank + k // 16384}", f'BINARY "{name}"']
        files.append(name)
    return asm


def pack_bricks():
    """each (set, position) block: a table of 24 offsets (one a row) from the
    start of the block's first page, then the images; a block may go on into
    the next page, an image never crosses a page's end"""
    data = bytearray()
    where = []                              # (first page, table address) per set x position
    stats = []

    def room(n):
        if len(data) // 8192 != (len(data) + n - 1) // 8192:
            data.extend(bytes(8192 - len(data) % 8192))

    for name, fn in SETS:
        size = 0
        for a in range(NANG):
            imgs = []
            for row in range(geom.ST_TOTAL):
                codes = geom.render(fn(a * 256 // STEPS, row))
                # pen offsets: lit faces base + 0, shaded ones base + 1
                imgs.append(encode((codes == geom.SHADE).astype(np.uint8), codes != 0))
            room(2 * len(imgs))
            page, table = len(data) // 8192, len(data)
            data.extend(bytes(2 * len(imgs)))
            for k, img in enumerate(imgs):
                room(len(img))
                off = len(data) - page * 8192
                assert off < 65536
                data[table + 2 * k:table + 2 * k + 2] = off.to_bytes(2, "little")
                data.extend(img)
                size += len(img)
            where.append((page, BRICK_WINDOW + table - page * 8192))
        stats.append((name, size))
    data.extend(bytes(-len(data) % 16384))
    pages = [bytes(data[i:i + 8192]) for i in range(0, len(data), 8192)]
    return pages, where, stats


def main():
    os.makedirs(os.path.join(ROOT, "gfx"), exist_ok=True)
    bd, bd_pal = backdrop()
    ghost = []
    for v in range(8):
        ghost += [rgb9(STACK_PAL[PEN_CORE]), rgb9(STACK_PAL[8 + 2 * v])]
    pal = [rgb9(c) for c in STACK_PAL] + [rgb9(STACK_PAL[PEN_CORE])] * 16 + ghost + bd_pal
    pal += [0] * (256 - len(pal))
    pal = avoid_transparent(pal)

    asm = ["; generated by tools/gen_gfx.py - do not edit: Stack Tower's pictures,",
           "; straight into Next memory banks by the .nex", ""]

    # the first screen: the backdrop and the view at step 0, the panel
    scr = np.full((256, 320), PEN_PANEL, np.uint8)
    view = bd.copy()
    draw_core(view, 0)
    scr[:, :200] = view
    asm += bank_asm("scr", column_major(scr), SCR_BANK)

    # the base pictures
    base = bytearray()
    for step in range(STEPS):
        view = bd.copy()
        draw_core(view, step * 256 // STEPS)
        for x in (0, 199):
            assert not (view[:, x] != bd[:, x]).any(), "the core reaches the view's edge"
        base += column_major(view[:, BASE_X0:BASE_X0 + 128])
    asm += bank_asm("base", bytes(base), BASE_BANK)

    # the bricks, checked to stay in the base pictures' columns
    for _, fn in SETS:
        for a in (0, NANG - 1):
            for row in range(geom.ST_TOTAL):
                cols = np.nonzero(geom.render(fn(a * 256 // STEPS, row)).any(axis=0))[0]
                if len(cols):
                    assert cols.min() >= BASE_X0 and cols.max() < BASE_X0 + 128, (a, row, cols)
    pages, where, stats = pack_bricks()
    asm += bank_asm("brk", b"".join(pages), BRICK_BANK)
    title_bank = BRICK_BANK + len(pages) // 2
    assert title_bank + 5 <= 112, "more than a 2 MB Next has"

    # the title painting, its own palette
    tim = np.asarray(Image.open(os.path.join(ROOT, "assets", "title_320x256.png")).convert("RGB"))
    tidx, tpal = picture_palette(tim, 255, 1)
    tpal = avoid_transparent([0] + tpal + [0] * (255 - len(tpal)))
    asm += bank_asm("title", column_major(tidx.astype(np.uint8)), title_bank)
    # the palettes, in a bank of their own (main memory is full), then the
    # drop guide's rows: for each position round the tower (0..40 eighths)
    # and each edge between rows (0..24), the first pixel row below the edge
    # on the core, in the column's middle
    pal_bytes = b"".join(int(c).to_bytes(2, "little") for c in pal + tpal)
    for a in range(NANG):
        for row in range(geom.ST_TOTAL + 1):
            y = math.ceil(geom.core_edge_y(a * 256 // STEPS, row) - 0.5)
            pal_bytes += bytes([min(255, max(0, y))])
    asm += bank_asm("pal", pal_bytes + bytes(16384 - len(pal_bytes)), PAL_BANK)
    open(os.path.join(ROOT, "src", "gfx_data.asm"), "w").write("\n".join(asm) + "\n")

    def arr(name, typ, vals, per=12):
        rows = [", ".join(str(v) for v in vals[i:i + per]) for i in range(0, len(vals), per)]
        return f"const {typ} {name}[{len(vals)}] = {{\n    " + ",\n    ".join(rows) + "\n};\n"

    c = ["/* generated by tools/gen_gfx.py - do not edit */", '#include "gfx_data.h"', ""]
    c.append(arr("brick_page", "uint8_t", [BRICK_BANK * 2 + p for p, _ in where], 16))
    c.append(arr("brick_addr", "uint16_t", [a for _, a in where], 8))
    open(os.path.join(ROOT, "src", "gfx_data.c"), "w").write("\n".join(c))
    h = f"""/* generated by tools/gen_gfx.py - do not edit */
#ifndef GFX_DATA_H
#define GFX_DATA_H
#include <stdint.h>

#define GFX_STEPS       {STEPS}       /* view steps a column */
#define GFX_NANG        {NANG}      /* brick positions: 0 .. 5 columns round */
#define GFX_SCR_BANK    {SCR_BANK}      /* the first screen (and Layer 2 buffer A) */
#define GFX_BACK_BANK   {BACK_BANK}      /* Layer 2 buffer B */
#define GFX_BASE_PAGE   {BASE_BANK * 2}      /* base picture of step s: 4 pages from GFX_BASE_PAGE + 4 s */
#define GFX_BASE_X0     {BASE_X0}      /* ... columns 32..159 */
#define GFX_TITLE_BANK  {title_bank}      /* the title painting, 5 banks */
#define GFX_VIEW_MIRROR 199     /* a brick on the left: the right one mirrored about 99.5 */

enum {{ SET_BOARD, SET_PIECE, SET_GHOST }};
/* the block of brick images of set s at position a: page p = brick_page[s * GFX_NANG + a]
   mapped at 0x0000 (MMU slot 0, over the ROM), at brick_addr[...]: 24 offsets (one a row)
   from the start of page p: row r's image is in page p + (offset >> 13) at offset & 0x1FFF */
extern const uint8_t brick_page[{len(where)}];
extern const uint16_t brick_addr[{len(where)}];
#define GFX_PAL_PAGE    {PAL_BANK * 2}     /* the game's palette (256 RGB333 words), then the title painting's */
#define GFX_GUIDE_Y     1024    /* in that page: the guide's rows, [41][25] (see gen_gfx.py) */
#define GFX_PEN_SLOT    {PEN_SLOT}      /* the core's panels: pen 32 + k for the column k to the right */
#define GFX_PEN_GHOST   {PEN_GHOST}      /* the ghost of piece type t: pens 48 + 2 t (core) and + 1 (its colour) */
#define GFX_CACHE_PAGE  {CACHE_BANK * 2}     /* 4 pages: the board as last drawn (tower.c) */
#define GFX_MUSIC_BANK  {MUSIC_BANK}      /* the song's rows (tools/make_music.py) */

#endif
"""
    open(os.path.join(ROOT, "src", "gfx_data.h"), "w").write(h)
    for name, size in stats:
        print(f"{name}: {size} bytes")
    print(f"brick pages {len(pages)}, title in banks {title_bank}..{title_bank + 4}")


if __name__ == "__main__":
    main()
