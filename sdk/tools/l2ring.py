"""Pictures for sdk/include/l2ring.h: a big picture of palette indices,
run-length coded down every column and along every row, with a directory of
both, for Layer 2's 320 x 256 ring.

    from l2ring import encode, decode, screen

  encode(idx, palette, void, ox=0, oy=0) -> bytes (padded to 8K pages)
      idx: rows of palette indices (lists or a 2D numpy array), < 0xE3
      (Layer 2's transparent colour); palette: up to 227 9-bit RGB333
      colours; void: the index shown off the picture; ox, oy: two signed
      16-bit numbers stored for the program (lr_ox, lr_oy)
  decode(data) -> (idx rows from the column runs, idx rows from the row runs,
      palette, void, ox, oy): the reader's view of both, for tests
  screen(idx, void, x, y) -> 256 rows of 320: what the screen shows with its
      top left at picture pixel (x, y)

Format (little-endian): u16 w, h; i16 ox, oy; u8 void; u8 n; u16 palette[n];
then w + h directory entries of 3 bytes (page from the first, offset in the
page) to the runs of each column, then each row; runs are (length 1..255,
index) pairs. Test: python3 sdk/tools/test_tools.py; projects/ringtest.
"""
import struct

PAGE = 8192


def _runs(line):
    out = bytearray()
    i, n = 0, len(line)
    while i < n:
        j = i
        while j < n and j - i < 255 and line[j] == line[i]:
            j += 1
        out += bytes([j - i, int(line[i])])
        i = j
    return out


def encode(idx, palette, void, ox=0, oy=0):
    rows = [list(map(int, r)) for r in idx]
    h, w = len(rows), len(rows[0])
    assert len(palette) < 0xE3 and all(v < 0xE3 for r in rows for v in r)
    head = struct.pack("<HHhhBB", w, h, ox, oy, void, len(palette))
    head += struct.pack(f"<{len(palette)}H", *palette)
    data_at = len(head) + 3 * (w + h)
    body, dirs = bytearray(), bytearray()
    lines = [[rows[y][x] for y in range(h)] for x in range(w)] + rows
    for line in lines:
        o = data_at + len(body)
        dirs += bytes([o >> 13]) + struct.pack("<H", o & 0x1FFF)
        body += _runs(line)
    out = head + dirs + body
    return bytes(out + bytes(-len(out) % PAGE))


def decode(data):
    w, h, ox, oy, void, n = struct.unpack_from("<HHhhBB", data, 0)
    pal = list(struct.unpack_from(f"<{n}H", data, 10))
    d0 = 10 + 2 * n

    def line(k, length):
        page, off = data[d0 + 3 * k], struct.unpack_from("<H", data, d0 + 3 * k + 1)[0]
        p = page * PAGE + off
        out = []
        while len(out) < length:
            out += [data[p + 1]] * data[p]
            p += 2
        return out
    cols = [line(x, h) for x in range(w)]
    by_cols = [[cols[x][y] for x in range(w)] for y in range(h)]
    by_rows = [line(w + y, w) for y in range(h)]
    return by_cols, by_rows, pal, void, ox, oy


def screen(idx, void, x, y):
    h, w = len(idx), len(idx[0])
    return [[idx[yy][xx] if 0 <= yy < h and 0 <= xx < w else void for xx in range(x, x + 320)]
            for yy in range(y, y + 256)]
