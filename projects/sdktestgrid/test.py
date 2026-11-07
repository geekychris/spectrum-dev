"""sdktestgrid - the grid modules gridpack and gridbfs on a 48K.

- gridpack.py: repacking src/grids.txt gives src/grids_data.c byte for byte;
  every grid unpacks on the Spectrum (gridpack.c) as it was drawn, and in
  the packer's own reference decoder; random grids round-trip
- gridbfs: the first step from every cell towards a series of targets (each
  one cell from the last, so the field's "no path" marks are carried over),
  and after a cell's flags change, against a plain breadth-first search from
  each cell trying left, right, up, down (Bullion Dash's guards)
"""
import random
import re
import sys
from collections import deque
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(ROOT / "sdk" / "tools"))

from zxdev.emu import Emulator  # noqa: E402
import gridpack  # noqa: E402
import levelpack  # noqa: E402

failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


# ------------------------------------------------------------ gridpack on the host
w, h, rooms = levelpack.parse((HERE / "src" / "grids.txt").read_text())
pk = gridpack.pack([c for _, c in rooms], w, h, 6)
c, hdr = gridpack.c_source(pk, "tg", [nm for nm, _ in rooms], "grids_data.h")
check("gridpack: repacking grids.txt gives grids_data.c", c == (HERE / "src" / "grids_data.c").read_text())
check("gridpack: reference decoder", all(gridpack.unpack(pk, i) == cells for i, (_, cells) in enumerate(rooms)))
rnd = random.Random(7)
ok = True
for trial in range(20):
    gw, gh, n = rnd.randint(1, 20), rnd.randint(1, 12), rnd.randint(1, 15)
    grids = [bytes(rnd.choice([0, 0, 0, rnd.randrange(n)]) for _ in range(gw * gh)) for _ in range(rnd.randint(1, 5))]
    p = gridpack.pack(grids, gw, gh, n)
    ok = ok and all(gridpack.unpack(p, i) == g for i, g in enumerate(grids))
check("gridpack: random grids (sizes, tile counts) round-trip", ok)
try:
    gridpack.pack([bytes([16])], 1, 1, 16)
    check("gridpack: refuses tile ids past 15", False)
except ValueError:
    check("gridpack: refuses tile ids past 15", True)

# ------------------------------------------------------------ the references for gridbfs
IN, L, R, U, D = 16, 1, 2, 4, 8
FLAGS = [IN | L | R | U | D, 0, IN | R, IN | D, IN | L | R | U | D, L | R]
TARGETS = [(0, 0), (1, 0), (1, 1), (6, 6)]


def first_step(fl, s, t):
    """The original Bullion Dash search: BFS from s, neighbours L R U D."""
    if s == t:
        return "*"
    par = {s: None}
    q = deque([(s, 0)])
    while q:
        c, d = q.popleft()
        if c == t:
            while par[c][0] != s:
                c = par[c][0]
            return par[c][1]
        if d >= 252:
            continue
        for dx, dy, bit, name in ((-1, 0, L, "L"), (1, 0, R, "R"), (0, -1, U, "U"), (0, 1, D, "D")):
            n = (c[0] + dx, c[1] + dy)
            if n in fl and n not in par and fl[c] & bit and fl[n] & IN:
                par[n] = (c, name)
                q.append((n, d + 1))
    return "-"


def expected(cells):
    fl = {(x, y): FLAGS[cells[y * w + x]] for y in range(h) for x in range(w)}
    out = []
    for tag, targets in (("a", TARGETS), ("b", [(6, 6)])):
        if tag == "b":
            fl[(4, 0)] = 0
        for t in targets:
            for y in range(h):
                out.append(f"bfs {tag} {t[0]},{t[1]} {y} " + "".join(first_step(fl, (x, y), t) for x in range(w)))
    return out


# ------------------------------------------------------------ on the Spectrum
with Emulator() as e:
    e.load(HERE / "build" / "sdktestgrid.tap")
    check("runs to the end", e.run_until("ZX:DONE", 20000))
    e.run(2)
    log = e.log_text(0)

for g, (name, cells) in enumerate(rooms):
    got = [m.group(1) for m in re.finditer(rf"grid {g} \d+ (\d+)", log)]
    want = ["".join(str(v) for v in cells[y * w:(y + 1) * w]) for y in range(h)]
    check(f"gridpack.c unpacks {name}", got == want, f"{got[:2]} vs {want[:2]}")

pos = 0
lines = [l for l in log.splitlines() if l.startswith("bfs ")]
per = len(TARGETS) * h + h
for g, (name, cells) in enumerate(rooms):
    want = expected(cells)
    got = lines[g * per:(g + 1) * per]
    bad = [(a, b) for a, b in zip(got, want) if a != b]
    check(f"gridbfs steps on {name} (moving targets, a changed cell)", got == want and not bad,
          f"{len(got)} lines; first difference {bad[:1]}")
m = re.findall(r"searched \d+ (\d+) overflow=(\d)", log)
check("no queue overflow", m and all(o == "0" for _, o in m), str(m))

print(f"{'FAIL' if failures else 'PASS'}: sdktestgrid")
sys.exit(1 if failures else 0)
