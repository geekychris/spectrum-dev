"""ASCII rooms -> packed tile maps for sdk/include/tmap.h (tmap_load).

A level file:

    ; comment (a line starting with ';' - '#' is a tile in most games)
    size 20 15                  ; cells per room, the same for every room
    legend . 0  # 1  K 2  P 8   ; character -> tile id, in pairs
    room FIRST SHAFT            ; a room: its name, then `size` rows
    ####################        (text after a row's first space is ignored)
    #..................#
    ...

    python3 levelpack.py levels.txt --prefix lv -o levels_data.c [--header levels_data.h]

writes, for N rooms, `const uint8_t lv_map_0[]` ... (zxrle streams),
`const uint8_t *const lv_maps[N]`, `const char *const lv_names[N]` and
`#define LV_COUNT N`, `LV_W`, `LV_H` in the header. As a module:
`parse(text) -> (w, h, rooms)` with rooms = [(name, bytes)], `pack_rooms`.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from zxrle import pack  # noqa: E402


class LevelError(ValueError):
    pass


def parse(text):
    w = h = None
    legend = {}
    rooms = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        raw = lines[i]
        i += 1
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        word, _, rest = line.partition(" ")
        if word == "size":
            w, h = (int(v) for v in rest.split())
        elif word == "legend":
            toks = rest.split()
            if len(toks) % 2:
                raise LevelError(f"line {i}: legend needs character/id pairs")
            for ch, v in zip(toks[::2], toks[1::2]):
                if len(ch) != 1:
                    raise LevelError(f"line {i}: legend key {ch!r} is not one character")
                legend[ch] = int(v, 0)
        elif word == "room":
            if w is None or not legend:
                raise LevelError(f"line {i}: `size` and `legend` come before the first room")
            name = rest.strip()
            if not name:
                raise LevelError(f"line {i}: a room needs a name")
            cells = bytearray()
            for r in range(h):
                if i >= len(lines):
                    raise LevelError(f"room {name!r}: ends after {r} rows, needs {h}")
                row = lines[i].rstrip("\n")
                i += 1
                row = row.split(None, 1)[0] if row.strip() else ""
                if len(row) != w:
                    raise LevelError(f"room {name!r} row {r}: {len(row)} cells, needs {w}")
                for ch in row:
                    if ch not in legend:
                        raise LevelError(f"room {name!r} row {r}: {ch!r} is not in the legend")
                    cells.append(legend[ch])
            rooms.append((name, bytes(cells)))
        else:
            raise LevelError(f"line {i}: unknown {word!r}")
    if not rooms:
        raise LevelError("no rooms")
    return w, h, rooms


def c_bytes(data, indent="    "):
    out = []
    for k in range(0, len(data), 16):
        out.append(indent + ", ".join(f"0x{b:02X}" for b in data[k:k + 16]) + ",")
    return "\n".join(out)


def pack_rooms(w, h, rooms, prefix, header_name=None):
    P = prefix.upper()
    c = [f"/* made by sdk/tools/levelpack.py - edit the level file, not this */",
         "#include <stdint.h>"]
    if header_name:
        c.append(f'#include "{header_name}"')
    total = 0
    for n, (name, cells) in enumerate(rooms):
        packed = pack(cells)
        total += len(packed)
        c.append(f"\n/* {name}: {len(cells)} -> {len(packed)} bytes */")
        c.append(f"const uint8_t {prefix}_map_{n}[] = {{\n{c_bytes(packed)}\n}};")
    c.append(f"\nconst uint8_t *const {prefix}_maps[{len(rooms)}] = {{ "
             + ", ".join(f"{prefix}_map_{n}" for n in range(len(rooms))) + " };")
    c.append(f"const char *const {prefix}_names[{len(rooms)}] = {{ "
             + ", ".join('"' + name.replace('"', "'") + '"' for name, _ in rooms) + " };")
    hdr = [f"/* made by sdk/tools/levelpack.py */",
           f"#ifndef {P}_DATA_H", f"#define {P}_DATA_H", "#include <stdint.h>",
           f"#define {P}_COUNT {len(rooms)}", f"#define {P}_W {w}", f"#define {P}_H {h}",
           f"extern const uint8_t *const {prefix}_maps[{len(rooms)}];",
           f"extern const char *const {prefix}_names[{len(rooms)}];", "#endif"]
    return "\n".join(c) + "\n", "\n".join(hdr) + "\n", total


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("levels")
    ap.add_argument("--prefix", default="lv")
    ap.add_argument("-o", "--output", required=True)
    ap.add_argument("--header")
    a = ap.parse_args(argv)
    w, h, rooms = parse(Path(a.levels).read_text())
    hname = Path(a.header).name if a.header else None
    c, hdr, total = pack_rooms(w, h, rooms, a.prefix, hname)
    Path(a.output).write_text(c)
    if a.header:
        Path(a.header).write_text(hdr)
    print(f"{len(rooms)} rooms {w}x{h}: {len(rooms) * w * h} -> {total} bytes")


if __name__ == "__main__":
    main()
