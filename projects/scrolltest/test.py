"""sdk l2scroll.h: bands of Layer 2 (320 x 256) scrolled on their own. The
program fills each band's columns with the world x they show and checks its
ring after goto / move / jump / redraw; this checks its log, Layer 2's
memory, the copper list (a wait and two register moves a band) and the
picture: each band shifted by its own offset."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

NEX = Path(__file__).resolve().parent / "build" / "scrolltest.nex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def nums(log, key):
    m = re.search(rf"scroll: {key} (.*)", log)
    return [int(v) for v in re.findall(r"\d+", m.group(1))] if m else []


with Emulator(machine="next") as e:
    e.load(NEX)
    check("runs to the end", e.run_until("ZX:DONE", 300))
    e.run(2)
    log = e.log(0)
    g = nums(log, "goto")
    check("goto draws all 320 columns of each band", g[:1] == [960], str(g))
    check("goto: every column shows its world x (3 bands)", g[1:4] == [0, 0, 0], str(g))
    check("goto: offsets are x mod 320 (70000 too, past 16 bits)", g[4:] == [0, 1000 % 320, 70000 % 320], str(g))
    m = nums(log, "move")
    check("move right 3 / left 10: only 13 columns drawn", m[:1] == [13], str(m))
    check("move: the rings show the new positions", m[1:3] == [0, 0], str(m))
    check("move: offsets", m[3:] == [1003 % 320, 69990 % 320], str(m))
    j = nums(log, "jump")
    check("a move of a screen or more redraws everything", j == [320, 0, 1503 % 320], str(j))
    r = nums(log, "redraw")
    check("redraw: only a column in view is drawn", r[:1] == [1], str(r))
    col = r[1] if len(r) > 1 else 0
    page = 32 + col // 32
    v = e.read_mem((col % 32) * 256 + 176, 1, page=page)[0]
    check("redraw: the column in Layer 2 memory", v == 0x1C, hex(v))
    cn = nums(log, "copper")
    check("copper: a wait + 2 moves a band (the top band no wait)", cn == [8], str(cn))

    w = nums(log, "wrap goto")
    check("wrap: goto 1900 (all 320, columns wrap at 1920)", w == [320, 0, 1900 % 320], str(w))
    w = nums(log, "wrap right")
    check("wrap: 1900 -> 10 goes 30 right across the seam", w == [30, 0, (1900 + 30) % 320], str(w))
    w = nums(log, "wrap left")
    check("wrap: 10 -> 1910 goes 20 left back across", w == [20, 0, 1910 % 320], str(w))
    c = nums(log, "col")
    off = 1910 % 320
    check("l2s_col: world 1915 and 100 (wrapped) in view, 1000 not",
          c == [(off + 5) % 320, (off + 1920 - 1910 + 100) % 320, 0xFFFF], str(c))
    gn = nums(log, "gaps copper")
    check("rows in no band get scroll 0 (2 + 4 x 3 instructions)", gn == [14], str(gn))

    # the picture: row y of each band shows world x0 + screen x
    img = e.screenshot(layer="screen")
    W, H = img.size
    sc = W // 352
    def px(x, y):
        return img.getpixel(((16 + x) * sc + sc // 2, (24 + y) * sc + sc // 2))
    def rgb332(v):
        return (v >> 5, (v >> 2) & 7, v & 3)

    def seen(p):                         # an RGB pixel back to RRRGGGBB fields
        return (round(p[0] * 7 / 255), round(p[1] * 7 / 255), round(p[2] * 7 / 255) >> 1)

    for name, y, x0 in (("middle", 100, 1503), ("bottom", 200, 69990)):
        good = 0
        for x in range(320):
            v = (x0 + x) & 0xFF
            v = 0xE2 if v == 0xE3 else v
            good += seen(px(x, y)) == rgb332(v)
        check(f"the {name} band shows world x {x0}.. (its own offset {x0 % 320})", good >= 316, f"{good}/320")

print(f"{'FAIL' if failures else 'PASS'}: scrolltest")
sys.exit(1 if failures else 0)
