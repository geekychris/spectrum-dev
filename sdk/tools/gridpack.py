"""Tile grids (levels, rooms) -> one packed set for sdk/include/gridpack.h.

For many grids of the same size whose tiles are small ids (< 16). Each tile is
coded with a binary arithmetic coder and fixed probabilities chosen by
context, all grids sharing one model:

    is it the tile before?         (by the tile before and the tile above)
    else, is it the tile above?    (same context; asked when they differ)
    else, which tile: "is it X?" in the set's order of frequency, skipping
    the two already ruled out      (by the tile above)

"The tile before" is the previous one in the row-major stream; at the edges
the missing neighbour is the value ntiles. Bullion Dash's 150 levels of
28 x 16 (33.6 KB as nibbles) take ~8 KB; zxrle gives ~20 KB, Huffman codes
with the same contexts ~10.4 KB.

Input is a levelpack.py level file (`size`, `legend`, `room` lines):

    python3 gridpack.py levels.txt --prefix lv -o levels_data.c --header levels_data.h

writes `const gp_set lv_set` (unpack grid i with gp_unpack(&lv_set, i, out)),
`const char *const lv_names[N]` (unless --no-names) and `#define LV_COUNT N`,
`LV_W`, `LV_H`.

As a module: `pack(grids, w, h, ntiles)` -> a Packed (`size`, C source via
`c_source`), `unpack(packed, i)` -> grid i (the reference decoder, the same
steps as gridpack.c).

The coder: a range R (16 bits, kept >= 0x8000) and the code value C; a
decision with probability p/256 of a 0 splits R at bound = (R >> 8) * p.
The encoder works on Python integers of any size, so it has no carries.
"""
import argparse
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from levelpack import parse  # noqa: E402


class Packed:
    pass


def _decisions(grid, w, n, order):
    """Yield (table, index, bit) for each binary decision coding the grid."""
    prev = n
    for i, t in enumerate(grid):
        above = grid[i - w] if i >= w else n
        ctx = prev * (n + 1) + above
        yield "a", ctx, int(t == prev)
        if t != prev:
            if above != prev and above != n:
                yield "b", ctx, int(t == above)
            if t != above or above == prev or above == n:
                cand = [s for s in order if s != prev and not (s == above and above != n)]
                for k, s in enumerate(cand[:-1]):
                    yield "l", above * n + order.index(s), int(t == s)
                    if t == s:
                        break
        prev = t


def _prob(c0, c1):
    """P(bit = 0) in 1/256ths, 1..255 (128 without data)."""
    if c0 + c1 == 0:
        return 128
    return min(255, max(1, round(256 * c0 / (c0 + c1))))


class _Encoder:
    def __init__(self):
        self.low, self.rng, self.shift = 0, 0xFFFF, 0

    def bit(self, p, b):
        bound = (self.rng >> 8) * p
        if b == 0:
            self.rng = bound
        else:
            self.low += bound
            self.rng -= bound
        while self.rng < 0x8000:
            self.rng <<= 1
            self.low <<= 1
            self.shift += 1

    def finish(self):
        """The stream: 16 + shift bits of a value in [low, low + rng), as few
        as can be left out (the decoder reads whatever follows them)."""
        nbits = 16 + self.shift
        for j in range(16, -1, -1):        # the most trailing zeros that still fit
            v = -(-self.low // (1 << j)) << j
            if v + (1 << j) <= self.low + self.rng:
                break
        keep = max(nbits - j, 1)
        v >>= nbits - keep
        return v, keep


def _encode(grid, w, n, order, tables):
    enc = _Encoder()
    for tab, idx, b in _decisions(grid, w, n, order):
        enc.bit(tables[tab][idx], b)
    return enc.finish()


def pack(grids, w, h, ntiles):
    """Pack grids (each w*h tile ids < ntiles <= 15)."""
    if not 1 <= ntiles <= 15:
        raise ValueError("ntiles must be 1..15")
    grids = [bytes(g) for g in grids]
    for k, g in enumerate(grids):
        if len(g) != w * h or any(v >= ntiles for v in g):
            raise ValueError(f"grid {k}: needs {w * h} tile ids < {ntiles}")
    n = ntiles
    freq = Counter(t for g in grids for t in g)
    order = sorted(range(n), key=lambda s: (-freq[s], s))
    counts = {"a": Counter(), "b": Counter(), "l": Counter()}
    for g in grids:
        for tab, idx, b in _decisions(g, w, n, order):
            counts[tab][(idx, b)] += 1
    sizes = {"a": (n + 1) * (n + 1), "b": (n + 1) * (n + 1), "l": (n + 1) * n}
    tables = {t: bytes(_prob(counts[t][(i, 0)], counts[t][(i, 1)]) for i in range(sizes[t])) for t in sizes}
    bits, grid_off = bytearray(), []
    acc, nacc = 0, 0
    for g in grids:
        if nacc:                                # each grid starts on a byte
            bits.append((acc << (8 - nacc)) & 0xFF)
            acc, nacc = 0, 0
        grid_off.append(len(bits))
        v, nb = _encode(g, w, n, order, tables)
        acc, nacc = (acc << nb) | v, nacc + nb
        while nacc >= 8:
            nacc -= 8
            bits.append((acc >> nacc) & 0xFF)
        acc &= (1 << nacc) - 1
    if nacc:
        bits.append((acc << (8 - nacc)) & 0xFF)
    bits += b"\0\0"                             # the last grid's decoder reads past its end
    pk = Packed()
    pk.w, pk.h, pk.n, pk.count = w, h, n, len(grids)
    pk.order, pk.pa, pk.pb, pk.pl = bytes(order), tables["a"], tables["b"], tables["l"]
    pk.grid_off, pk.bits = grid_off, bytes(bits)
    pk.size = len(pk.order) + len(pk.pa) + len(pk.pb) + len(pk.pl) + 2 * len(grid_off) + len(pk.bits)
    return pk


def unpack(pk, index):
    """The reference decoder: the same steps as sdk/src/gridpack.c."""
    n, w, size = pk.n, pk.w, pk.w * pk.h
    pos = pk.grid_off[index] * 8

    def nextbit():
        nonlocal pos
        byte = pk.bits[pos >> 3] if (pos >> 3) < len(pk.bits) else 0
        pos += 1
        return (byte >> (7 - ((pos - 1) & 7))) & 1

    rng, code = 0xFFFF, 0
    for _ in range(16):
        code = (code << 1) | nextbit()

    def bit(p):
        nonlocal rng, code
        bound = (rng >> 8) * p
        if code < bound:
            rng, b = bound, 0
        else:
            code -= bound
            rng -= bound
            b = 1
        while rng < 0x8000:
            rng <<= 1
            code = ((code << 1) | nextbit()) & 0xFFFF
        return b

    out, prev = [], n
    while len(out) < size:
        above = out[-w] if len(out) >= w else n
        ctx = prev * (n + 1) + above
        if bit(pk.pa[ctx]):
            t = prev
        elif above != prev and above != n and bit(pk.pb[ctx]):
            t = above
        else:
            ruled = above if above != n else prev
            t = None
            last = None
            for k, s in enumerate(pk.order):
                if s == prev or s == ruled:
                    continue
                last = s
                rest = [x for x in pk.order[k + 1:] if x != prev and x != ruled]
                if not rest or bit(pk.pl[above * n + k]):
                    t = s
                    break
            if t is None:
                t = last
        out.append(t)
        prev = t
    return bytes(out)


def _c_bytes(data, indent="    "):
    return "\n".join(indent + ", ".join(f"0x{b:02X}" for b in data[k:k + 16]) + ","
                     for k in range(0, len(data), 16))


def c_source(pk, prefix, names=None, header_name=None):
    """(C source, header) defining `const gp_set <prefix>_set`."""
    P = prefix.upper()
    c = ["/* made by sdk/tools/gridpack.py - edit the level file, not this */",
         "#include <stdint.h>", '#include "gridpack.h"']
    if header_name:
        c.append(f'#include "{header_name}"')
    offs = "\n".join("    " + ", ".join(str(x) for x in pk.grid_off[k:k + 12]) + ","
                     for k in range(0, len(pk.grid_off), 12))
    c += [f"\n/* {pk.count} grids {pk.w}x{pk.h}: {pk.size} bytes */",
          f"static const uint8_t {prefix}_order[] = {{\n{_c_bytes(pk.order)}\n}};",
          f"static const uint8_t {prefix}_pa[] = {{\n{_c_bytes(pk.pa)}\n}};",
          f"static const uint8_t {prefix}_pb[] = {{\n{_c_bytes(pk.pb)}\n}};",
          f"static const uint8_t {prefix}_pl[] = {{\n{_c_bytes(pk.pl)}\n}};",
          f"static const uint16_t {prefix}_grid_off[] = {{\n{offs}\n}};",
          f"static const uint8_t {prefix}_bits[] = {{\n{_c_bytes(pk.bits)}\n}};",
          f"const gp_set {prefix}_set = {{ {pk.w}, {pk.h}, {pk.n}, {prefix}_order, {prefix}_pa, "
          f"{prefix}_pb, {prefix}_pl, {prefix}_grid_off, {prefix}_bits }};"]
    hdr = ["/* made by sdk/tools/gridpack.py */", f"#ifndef {P}_DATA_H", f"#define {P}_DATA_H",
           '#include "gridpack.h"', f"#define {P}_COUNT {pk.count}", f"#define {P}_W {pk.w}",
           f"#define {P}_H {pk.h}", f"extern const gp_set {prefix}_set;"]
    if names is not None:
        c.append(f"const char *const {prefix}_names[{len(names)}] = {{\n"
                 + "\n".join(f'    "{nm}",' for nm in (x.replace('"', "'") for x in names)) + "\n};")
        hdr.append(f"extern const char *const {prefix}_names[{len(names)}];")
    hdr.append("#endif")
    return "\n".join(c) + "\n", "\n".join(hdr) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("levels", help="a levelpack.py level file")
    ap.add_argument("--prefix", default="lv")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--header")
    ap.add_argument("--no-names", action="store_true", help="leave out the room names")
    a = ap.parse_args(argv)
    w, h, rooms = parse(Path(a.levels).read_text())
    ntiles = max(max(cells) for _, cells in rooms) + 1
    pk = pack([cells for _, cells in rooms], w, h, ntiles)
    for i, (_, cells) in enumerate(rooms):
        if unpack(pk, i) != cells:
            raise SystemExit(f"internal error: grid {i} does not unpack")
    hname = Path(a.header).name if a.header else None
    c, hdr = c_source(pk, a.prefix, None if a.no_names else [nm for nm, _ in rooms], hname)
    Path(a.output).write_text(c)
    if a.header:
        Path(a.header).write_text(hdr)
    print(f"{len(rooms)} grids {w}x{h}: {len(rooms) * w * h} -> {pk.size} bytes")


if __name__ == "__main__":
    main()
