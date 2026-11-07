#!/usr/bin/env python3
"""A frame of the tower drawn on the host the way the Next draws it (the
base picture, then the brick images in the game's order), from the files
gen_gfx.py wrote: to check the pictures and the drawing order.

    python3 tools/preview.py out.png [view]
"""
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "..", "..", "sdk", "tools"))
from segimg import decode  # noqa: E402

STEPS, NANG = 8, 41


def load_pal():
    src = open(os.path.join(ROOT, "src", "gfx_data.c")).read()
    body = src.split("game_pal[256] = {")[1].split("}")[0]
    vals = [int(v) for v in body.replace("\n", " ").split(",") if v.strip()]
    return np.array([[(c >> 6 & 7) * 255 // 7, (c >> 3 & 7) * 255 // 7, (c & 7) * 255 // 7] for c in vals], np.uint8)


def load_banks(prefix):
    data, k = b"", 0
    while os.path.exists(os.path.join(ROOT, "gfx", f"{prefix}_{k}.bin")):
        data += open(os.path.join(ROOT, "gfx", f"{prefix}_{k}.bin"), "rb").read()
        k += 1
    return data


def brick_image(bricks, where, s, a, row):
    page, addr = where[s * NANG + a]
    pg = bricks[page * 8192:(page + 1) * 8192]
    off = addr - 0x4000
    img = int.from_bytes(pg[off + 2 * row:off + 2 * row + 2], "little") - 0x4000
    return pg[img:]


def draw_order(cols):
    """(column, rel step) of the columns in view, the furthest round first"""
    return sorted(cols, key=lambda c: -abs(c[1]))


ROWS_ORDER = list(range(23, 11, -1)) + list(range(0, 12))


def frame(board, view, piece=(), ghost=()):
    pal = load_pal()
    scr = np.frombuffer(load_banks("scr"), np.uint8).reshape(320, 256).T.copy()
    base = load_banks("base")
    bricks = load_banks("brk")
    src = open(os.path.join(ROOT, "src", "gfx_data.c")).read()
    pages = [int(v) for v in src.split("brick_page[")[1].split("{")[1].split("}")[0].replace("\n", " ").split(",") if v.strip()]
    addrs = [int(v) for v in src.split("brick_addr[")[1].split("{")[1].split("}")[0].replace("\n", " ").split(",") if v.strip()]
    where = [(p - pages[0], a) for p, a in zip(pages, addrs)]
    vq = view & ~(256 // STEPS - 1)
    step = (vq & 255) * STEPS // 256
    b = base[step * 32768:(step + 1) * 32768]
    scr[:, 32:160] = np.frombuffer(b, np.uint8).reshape(128, 256).T

    def put(s, c, row, shade, lit):
        rel = ((c * 256 - vq + 8 * 256) & 4095) - 8 * 256
        a = rel * STEPS // 256
        if abs(a) >= NANG:
            return
        img = brick_image(bricks, where, s, abs(a), row)
        for (x, y), code in decode(img, 199 if a < 0 else 0, a < 0).items():
            scr[y, x] = shade if code == 1 else lit

    def rel_of(c):
        return ((c * 256 - vq + 8 * 256) & 4095) - 8 * 256

    cols = draw_order([(c, rel_of(c)) for c in range(16)])
    for c, _ in cols:
        for row in ROWS_ORDER:
            v = board.get((c, row))
            if v:
                put(0, c, row, 8 + 2 * (v - 1) + 1 if v != 9 else 25, 8 + 2 * (v - 1) if v != 9 else 24)
    for c, row, v in ghost:
        put(2, c, row, 8 + 2 * (v - 1), 5)
    for c, row, v in piece:
        put(1, c, row, 8 + 2 * (v - 1) + 1, 8 + 2 * (v - 1))
    return Image.fromarray(pal[scr], "RGB")


if __name__ == "__main__":
    import random
    random.seed(3)
    board = {}
    for c in range(16):
        for r in range(random.randint(0, 6)):
            board[(c, r)] = random.randint(1, 9)
    view = int(sys.argv[2]) if len(sys.argv) > 2 else 3 * 256 + 80
    piece = [(3, 14, 5), (4, 14, 5), (3, 15, 5), (3, 13, 5), (2, 14, 5)]
    frame(board, view, piece).resize((640, 512), Image.NEAREST).save(sys.argv[1])
