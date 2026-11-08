"""sdk l2ring.h: a 700 x 520 picture scrolled on Layer 2 320 x 256. After each
move (right, down, left and off the left edge, up and off the top,
diagonals, a jump, past the bottom right, back) Layer 2's memory read through
the scroll registers must be l2ring.py's screen() of the picture exactly,
and only the new columns and rows were drawn."""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(ROOT / "sdk" / "tools"))
sys.path.insert(0, str(HERE / "tools"))

from zxdev.emu import Emulator  # noqa: E402
import l2ring  # noqa: E402
import gen  # noqa: E402

failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


pic = gen.picture()
stops = []
with Emulator(machine="next") as e:
    e.load(HERE / "build" / "ringtest.nex")
    check("opens the picture", e.run_until("ring: open", 100))
    e.run(1)
    m = re.search(r"open w=(\d+) h=(\d+) ox=(-?\d+) oy=(-?\d+) void=(\d+) colours=(\d+)", e.log(0))
    check("lr_open: size, ox, oy, void, palette", m is not None and m.groups() == ("700", "520", "123", "-45", "11", "12"),
          str(m and m.groups()))
    bad = []
    while True:
        mark = len(e.log(0))
        if not e.run_until("ring: at", 200):
            break
        e.run(1)
        m = re.search(r"ring: at (-?\d+) (-?\d+) cols=(\d+) rows=(\d+)", e.log(0)[mark:])
        x, y, cols, rows = (int(v) for v in m.groups())
        mem = b"".join(e.read_mem(0, 8192, page=18 + k) for k in range(10))
        sx, sy = e._reg(0x16) | (e._reg(0x71) & 1) << 8, e._reg(0x17)
        if (sx, sy) != (x % 320, y & 255):
            bad.append((x, y, "scroll", sx, sy))
        want = l2ring.screen(pic, 11, x, y)
        diff = 0
        for r in range(256):
            for c in range(320):
                if mem[((c + x) % 320) * 256 + ((r + y) & 255)] != want[r][c]:
                    diff += 1
        stops.append((x, y, cols, rows, diff))
        if diff:
            bad.append((x, y, diff))
    check(f"{len(stops)} stops, Layer 2 = the model at each (through the ring), scroll registers = x mod 320, y mod 256", len(stops) == 21 and not bad, str(bad[:4]))
    moves = {(s[0], s[1]): (s[2], s[3]) for s in stops}
    check("a first goto draws all 320 columns", moves.get((0, 0)) == (320, 0), str(moves.get((0, 0))))
    check("right 5: 5 columns; down 7: 7 rows; left 13: 13 columns", moves.get((5, 0)) == (5, 0) and
          moves.get((17, 7)) == (0, 7) and moves.get((4, 30)) == (13, 0), str(stops[:9]))
    check("a jump redraws", moves.get((330, 210)) == (320, 0), str(moves.get((330, 210))))
    check("ends", e.run_until("ZX:DONE", 50))

print(f"{'FAIL' if failures else 'PASS'}: ringtest")
sys.exit(1 if failures else 0)
