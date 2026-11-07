"""sdktestroom - the tile room modules: tmap, plat, cellspr48, levelpack.

- levelpack.py: repacking src/rooms.txt gives src/rooms_data.c byte for byte;
  rooms round-trip through zxrle; bad level files are refused with a reason
- tmap + plat: plat_cases.c logs the same lines on the Spectrum and built on
  the host (and the values are the ones worked out by hand below)
- cellspr48: the screen bytes and attributes of a room with a sprite over its
  tiles, after a move, a cs_refresh and the sprite switched off, against a
  Python model of the composition
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(ROOT / "sdk" / "tools"))

from zxdev.emu import Emulator  # noqa: E402
import levelpack  # noqa: E402
from zxrle import unpack  # noqa: E402

failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


# ------------------------------------------------------------ levelpack
text = (HERE / "src" / "rooms.txt").read_text()
w, h, rooms = levelpack.parse(text)
c, hdr, total = levelpack.pack_rooms(w, h, rooms, "rm", "rooms_data.h")
check("levelpack: repacking rooms.txt gives rooms_data.c", c == (HERE / "src" / "rooms_data.c").read_text())
check("levelpack: header", hdr == (HERE / "src" / "rooms_data.h").read_text())
check("levelpack: legend ids", rooms[0][1][:8] == bytes([1] * 8) and rooms[0][1][8 * 4 + 1] == 2)
streams = [bytes(int(v, 16) for v in re.findall(r"0x([0-9A-F]{2})", blk))
           for blk in re.findall(r"\{\n(.*?)\n\};", c, re.S)]
check("levelpack: rooms round-trip through zxrle", [unpack(s) for s in streams] == [r for _, r in rooms])
check("levelpack: an empty room packs small", len(streams[1]) <= 4, str(len(streams[1])))
bad = {
    "a row too short": "size 3 2\nlegend . 0\nroom A\n...\n..\n",
    "a character not in the legend": "size 2 1\nlegend . 0\nroom A\n.x\n",
    "too few rows": "size 2 2\nlegend . 0\nroom A\n..\n",
    "a room before size": "legend . 0\nroom A\n..\n",
    "no rooms": "size 2 2\nlegend . 0\n",
}
for what, t in bad.items():
    try:
        levelpack.parse(t)
        check(f"levelpack refuses {what}", False)
    except levelpack.LevelError:
        check(f"levelpack refuses {what}", True)
with tempfile.TemporaryDirectory() as d:
    r = subprocess.run([sys.executable, str(ROOT / "sdk" / "tools" / "levelpack.py"), str(HERE / "src" / "rooms.txt"),
                        "--prefix", "rm", "-o", f"{d}/o.c", "--header", f"{d}/o.h"], capture_output=True, text=True)
    check("levelpack: command line", r.returncode == 0 and Path(f"{d}/o.c").read_text().replace('"o.h"', '"rooms_data.h"')
          == (HERE / "src" / "rooms_data.c").read_text(), r.stderr)

# ------------------------------------------------------------ host build of the cases
sdk = ROOT / "sdk"
with tempfile.TemporaryDirectory() as d:
    exe = Path(d) / "cases"
    r = subprocess.run(["cc", "-std=c99", "-Wall", "-I", str(HERE / "src"), "-I", str(sdk / "include"), "-o", str(exe),
                        str(HERE / "src" / "host_main.c"), str(HERE / "src" / "plat_cases.c"),
                        str(HERE / "src" / "rooms_data.c"), str(sdk / "src" / "tmap.c"), str(sdk / "src" / "plat.c"),
                        str(sdk / "src" / "zxrle.c")], capture_output=True, text=True)
    check("host build of tmap/plat", r.returncode == 0, r.stderr[-300:])
    host = subprocess.run([str(exe)], capture_output=True, text=True).stdout if r.returncode == 0 else ""

cases_room = rooms[0][1]
expected = [
    f"tmap: unpacked={w * h}",
    f"tmap: walls={cases_room.count(1)} keys={cases_room.count(3)}",
    f"tmap: spawn=1 found={cases_room.index(2) % w},{cases_room.index(2) // w}",
    "tmap: replaced=1 now=0 at=9,1,9",          # off the map, the ledge at (3, 2), off the map
    "plat: kind=0,1,1,1",                       # open, the ledge, x < 0, past the right edge
]

# ------------------------------------------------------------ the Spectrum
SCR = 0x4000


def addr(col, row, line):
    return SCR | ((row & 0x18) << 8) | ((row & 7) << 5) | col | (line << 8)


TILES = [[0] * 8, [0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xFF], [0xAA, 0x55] * 4]
ATTRS = [7, (2 << 3) | 6, 4]
MAP = [[1, 1, 1, 1, 1, 1], [1, 0, 0, 2, 0, 1], [1, 0, 2, 2, 0, 1], [1, 1, 1, 1, 1, 1]]
CROSS = [0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81]
INK = 5 | 0x40                                  # INK_CYAN | BRIGHT


def model(sprite):
    """expected (bytes, attr) of every cell: its tile, the sprite's lines ORed
    in (a 16-bit window: the line shifted right by x & 7, byte j = col - x / 8),
    and the sprite's ink in the cells it draws into (paper kept)"""
    cells = {}
    for r in range(4):
        for col in range(6):
            t = list(TILES[MAP[r][col]])
            a = ATTRS[MAP[r][col]]
            if sprite:
                sx, sy = sprite
                j = col - (sx >> 3)
                last = 1 if sx & 7 else 0       # an aligned sprite is one byte wide
                lines = [k for k in range(8) if 0 <= r * 8 + k - sy < 8]
                if 0 <= j <= last and lines:
                    for k in lines:
                        window = (CROSS[r * 8 + k - sy] << 8) >> (sx & 7)
                        t[k] |= (window >> (8 * (1 - j))) & 0xFF
                    a = (a & 0x38) | INK
            cells[(col, r)] = (t, a)
    return cells


def screen_cells(e):
    out = {}
    for r in range(4):
        for col in range(6):
            t = [e.read_mem(addr(4 + col, 4 + r, k), 1)[0] for k in range(8)]
            a = e.read_mem(0x5800 + (4 + r) * 32 + 4 + col, 1)[0]
            out[(col, r)] = (t, a)
    return out


def compare(e, sprite, what):
    got, want = screen_cells(e), model(sprite)
    bad = [k for k in want if got[k] != want[k]]
    check(f"cellspr48: {what}", not bad, "; ".join(f"{k}: got {got[k]} want {want[k]}" for k in bad[:3]))


with Emulator() as e:
    e.load(HERE / "build" / "sdktestroom.tap")
    check("runs to the cases", e.run_until("room: drawn", 3000))
    e.run(1)
    log = e.log_text(0)
    zx = [l for l in log.splitlines() if l.startswith(("tmap:", "plat:"))]
    hl = [l for l in host.splitlines() if l.startswith(("tmap:", "plat:"))]
    check("tmap/plat: the Spectrum and the host log the same", zx == hl and len(zx) == 10, f"{zx} vs {hl}")
    for want in expected:
        check(want, want in zx, str(zx))
    falls = {m.group(1): m.groups()[1:] for m in (re.match(r"plat: (\w+) x=(-?\d+) y=(-?\d+) vy=(-?\d+) ground=(\d)", l) for l in zx) if m}
    check("plat: a fall from y = 61 snaps to the tile its top was in (y = 48, the original's snap)",
          falls.get("fall") == ("20", "48", "0", "1"), str(falls.get("fall")))
    check("plat: walking right stops at the wall block (column 6: x = 96 - 16)", falls.get("wall", ("",))[0] == "80", str(falls.get("wall")))
    check("plat: walking left stops at the outer wall (x = 16)", falls.get("left", ("",))[0] == "16", str(falls.get("left")))
    check("plat: a jump into a ceiling snaps and turns vy to 1", falls.get("ceiling", ("", "", ""))[1:3] == ("48", "1"), str(falls.get("ceiling")))
    compare(e, (13, 10), "drawn: tiles + a sprite over 4 cells, its ink in them")
    def at_step(name):
        """run to the program's next "room: <name>" line, its cell count"""
        if not e.run_until(f"room: {name}", 10):
            return None
        e.run(1)                                # the rest of the line (then it waits a frame)
        m = re.findall(rf"room: {name} \w+=(\d+)", e.log_text(0))
        return int(m[-1]) if m else None

    n = at_step("moved")
    check("cellspr48: a move redraws the 4 cells it left and the 1 it is in", n == 5, str(n))
    compare(e, (24, 16), "moved: old cells back to their tiles, the sprite in one cell")
    check("cellspr48: nothing changed, nothing drawn", at_step("still") == 0)
    check("cellspr48: cs_refresh marks the cell under the sprite", at_step("refresh") == 1)
    TILES[2][1:4] = [0x0F, 0xF0, 0x0F]
    check("cellspr48: the marked cell is composed by cs_update", at_step("refreshed") == 1)
    compare(e, (24, 16), "refresh: new lines 1..3 in every cell of the tile, composed under the sprite")
    n = at_step("off")
    check("cellspr48: switching off redraws its cell, cs_set another", n == 2, str(n))
    MAP[1][1] = 2
    compare(e, None, "switched off + cs_set: the tiles alone")
    check("finishes", e.run_until("ZX:DONE", 10))

print(f"{'FAIL' if failures else 'PASS'}: sdktestroom")
sys.exit(1 if failures else 0)
