#!/usr/bin/env python3
"""Column-run images for sdk/include/l2wrun.h (l2wr_blit): pictures whose
pixels are small offsets added to a base pen when drawn, so one image draws
in any of several colour sets laid out the same way in the palette (e.g. a
block lit / shaded at pens base + 0 / base + 1, for each colour of block).
Made for shapes drawn on the host for every view a game needs (Stack
Tower's 3D bricks): Layer 2's 320 mode is column-major, so a run down a
column is a straight copy.

Format:

    u8 x0, u8 w                 first column (added to the blit's x), width
    then for each of the w columns, its runs, then 0xFF:
      u8 ytop, u8 len (1..25), len bytes: pen offsets

encode(offsets, opaque) takes two 2D arrays (rows x columns): the offsets
and which pixels are drawn. Column c of the image is drawn at x + c
(mirrored: x - c); the first and last empty columns are left out (x0).
"""

MAX_RUN = 25                     # the blitter's unrolled loop


def encode(offsets, opaque):
    h = len(opaque)
    w = len(opaque[0]) if h else 0
    cols = [x for x in range(w) if any(opaque[y][x] for y in range(h))]
    if not cols:
        return bytes([0, 0])
    x0, x1 = cols[0], cols[-1]
    assert x0 < 256 and x1 - x0 + 1 < 256
    out = bytearray([x0, x1 - x0 + 1])
    for x in range(x0, x1 + 1):
        y = 0
        while y < h:
            if not opaque[y][x]:
                y += 1
                continue
            n = 0
            while y + n < h and opaque[y + n][x] and n < MAX_RUN:
                n += 1
            assert y < 255
            out += bytes([y, n]) + bytes(int(offsets[y + k][x]) for k in range(n))
            y += n
        out.append(0xFF)
    return bytes(out)


def decode(data, x=0, mirror=False):
    """bytes -> {(column, row): offset}, as l2wr_blit draws it at x"""
    x0, nw = data[0], data[1]
    px, i = {}, 2
    for k in range(nw):
        cx = x - x0 - k if mirror else x + x0 + k
        while data[i] != 0xFF:
            y, n = data[i], data[i + 1]
            for j in range(n):
                px[(cx, y + j)] = data[i + 2 + j]
            i += 2 + n
        i += 1
    return px
