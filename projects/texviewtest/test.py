"""The SDK's texview (./zx test texviewtest): a view made by
sdk/tools/texview.py (a disc, its texels from their places, every other
column shaded) drawn by tv_draw into a bank at four places - inside, off the
right and bottom, off the left and top, cut at tv_rows - with turns
(tv_off); then tv_or_span, tv_fill and tv_copy (forwards and turned round)
through Layer 2's write window. Every pixel of the bank against
texview.model() and the same steps in Python."""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(ROOT / "sdk" / "tools"))
sys.path.insert(0, str(HERE / "tools"))

from zxdev.emu import Emulator  # noqa: E402
from texview import model  # noqa: E402
from gen_data import disc  # noqa: E402

NEX = HERE / "build" / "texviewtest.nex"
OUT_PAGE = 40
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


tex = [[(u + 3 * v) & 31 for u in range(256)] for v in range(96)]
pages = disc().pages
want = {}
for x, y, off, rows in [(60, 40, 0, 192), (250, 185, 17, 192), (5, -3, 200, 192), (150, 95, 3, 100)]:
    model(pages, tex, off, x, y, rows, want)
for x in range(10, 20):
    want[(x, 100)] = want.get((x, 100), 0) | 0x40
for x in range(100, 108):
    want[(x, 110)] = want.get((x, 110), 0) | 0x40
for x in range(256):
    want[(x, 120)] = 0x33
for k in range(40):
    want[(7 + k, 126)] = tex[5][90 - k]
    want[(7 + k, 127)] = tex[6][10 + k]

with Emulator(machine="next") as e:
    e.load(NEX)
    check("runs", e.run_until("tv: done", 300))
    e.run(1)
    log = e.log(0)
    out = b"".join(e.read_mem(0, 8192, page=OUT_PAGE + k) for k in range(6))
    bad = [(x, y) for y in range(192) for x in range(256) if out[y * 256 + x] != want.get((x, y), 0)]
    check("every pixel as texview.model() and the spans say (clipped on all four sides, cut at tv_rows)",
          not bad, f"{len(bad)} wrong, first {bad[:4]}")
    n = [int(v) for v in re.findall(r"pixels=(\d+)", log)]
    full = disc().pixels
    check("tv_pixels counts what was drawn", len(n) == 4 and n[0] == full and 0 < n[1] < full and 0 < n[2] < full
          and 0 < n[3] < full, str(n))
    check("the views drawn inside are whole", sum(1 for (x, y) in want if 40 <= x <= 80 and 20 <= y <= 60) == full)

print(f"{'FAIL' if failures else 'PASS'}: texviewtest")
sys.exit(1 if failures else 0)
