#!/usr/bin/env python3
"""Column-segment images for sdk/include/l2wseg.h (l2ws_blit).

An image is a few flat colours given by codes 1..3 (0 = transparent), drawn
through the pens l2ws_pen[1..3] the program sets before blitting: the same
image draws a red brick or a blue one. Made for faces of 3D shapes drawn on
the host for each view (Stack Tower's bricks), which are mostly long runs of
one colour down a column of Layer 2's column-major 320 mode.

Format:

    u8 x0, u8 w                 first column (added to the blit's x), width
    then for each of the w columns:
      u8 ytop                   first row, or 0xFF: nothing in this column
      segments: u8 code << 6 | len (len 1..63; code 0 skips len rows)
      u8 0                      end of the column

encode(img, x0=0) takes a 2D array of codes (rows, columns); the image's
column c is drawn at x + x0 + c (mirrored: x - x0 - c).
"""


def encode(img):
    """img: 2D sequence of codes 0..3 (rows x columns) -> bytes. The first
    and last empty columns are trimmed; x0 is the first one kept."""
    h = len(img)
    w = len(img[0]) if h else 0
    cols = [x for x in range(w) if any(img[y][x] for y in range(h))]
    if not cols:
        return bytes([0, 0])
    x0, x1 = cols[0], cols[-1]
    assert x0 < 256 and x1 - x0 + 1 < 256
    out = bytearray([x0, x1 - x0 + 1])
    for x in range(x0, x1 + 1):
        ys = [y for y in range(h) if img[y][x]]
        if not ys:
            out.append(0xFF)
            continue
        assert ys[0] < 255
        out.append(ys[0])
        y = ys[0]
        while y <= ys[-1]:
            c, n = img[y][x], 0
            while y <= ys[-1] and img[y][x] == c and n < 63:
                y += 1
                n += 1
            assert 0 <= c <= 3
            out.append(c << 6 | n)
        out.append(0)
    return bytes(out)


def decode(data, x=0, mirror=False, w=320, h=256):
    """bytes -> {(column, row): code}, as l2ws_blit draws it at x"""
    x0, nw = data[0], data[1]
    px, i = {}, 2
    for k in range(nw):
        cx = x - x0 - k if mirror else x + x0 + k
        y = data[i]
        i += 1
        if y == 0xFF:
            continue
        while data[i]:
            code, n = data[i] >> 6, data[i] & 63
            for _ in range(n):
                if code:
                    px[(cx, y)] = code
                y += 1
            i += 1
        i += 1
    return px
