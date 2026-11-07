"""SDK modules for host-drawn 3D on the Next (./zx test nextruntest):
l2wrun (column-run images, base pen + offset, mirrored, across 8K pages:
compared pixel for pixel with sdk/tools/runimg.py's decode), dma_copy_pages
and sprtext (the 5x7 font as sprite patterns, shown zoomed)."""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(ROOT / "sdk" / "tools"))
sys.path.insert(0, str(HERE / "tools"))

from zxdev.emu import Emulator  # noqa: E402
from runimg import decode, encode  # noqa: E402
from gen_data import shape  # noqa: E402

NEX = HERE / "build" / "nextruntest.nex"
L2_PAGE, COPY_PAGE = 18, 70
FONT = {"A": [0x70, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x88]}
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


img = encode(*shape())
check("runimg: decode(encode(shape)) is the shape",
      decode(img) == {(x, y): o for y, row in enumerate(shape()[0]) for x, o in enumerate(row) if shape()[1][y][x]})

with Emulator(machine="next") as e:
    e.load(NEX)
    check("runs", e.run_until("run: done", 300))
    e.run(1)
    l2 = b"".join(e.read_mem(0, 8192, page=L2_PAGE + k) for k in range(10))
    want = {}
    for (x, y), o in decode(img, 10).items():
        want[(x, y)] = 100 + o
    for (x, y), o in decode(img, 150, True).items():
        want[(x, y)] = 200 + o
    bad = [(x, y) for x in range(320) for y in range(256) if l2[x * 256 + y] != want.get((x, y), 0)]
    check("l2wr_blit: every pixel as runimg.decode says (normal at 10, mirrored at 150, pages crossed)",
          not bad and len(want) > 2000, f"{len(bad)} wrong, first {bad[:3]}")
    check("dma_copy_pages: Layer 2's first two pages copied",
          e.read_mem(0, 16384, page=COPY_PAGE) == l2[:16384] and any(l2[:16384]))
    log = e.log(0)
    m = re.search(r"run: done next=(\d+) width=(\d+)", log)
    check("sprtext_show: one sprite a character, spaces skipped", m and m.group(1) == "8", log[-80:])
    check("sprtext_width: font5_width at 2x", m and m.group(2) == str(4 * 6 * 2 - 2))
    pat = e._zone_read(14, 0, 256)
    ok = True
    for r in range(16):
        for c in range(16):
            g = r < 7 and c < 5 and FONT["A"][r] & (0x80 >> c)
            s = 0 < r <= 7 and 0 < c <= 5 and FONT["A"][r - 1] & (0x80 >> (c - 1)) and not g
            ok &= pat[r * 16 + c] == (1 if g else 2 if s else 0xE3)
    check("sprtext_init: 'A' as a pattern (glyph 1, shadow 2, transparent 0xE3)", ok)
    attrs = e._zone_read(31, 0, 640)
    s5, s6, s7 = attrs[25:30], attrs[30:35], attrs[35:40]
    check("sprites 5-7 at x 100, 124 ('B' after the space), 136; y 50; palette 1; zoom x2",
          (s5[0], s6[0], s7[0], s5[1]) == (100, 124, 136, 50) and s5[2] >> 4 == 1 and s5[3] & 0xC0 == 0xC0
          and s5[3] & 63 == 0 and s6[3] & 63 == 1 and s7[3] & 63 == 2 and s5[4] & 0x1E == 0x0A,
          f"{list(s5)} {list(s6)} {list(s7)}")
    e.screenshot(HERE / "build" / "screen.png")

print(f"{'FAIL' if failures else 'PASS'}: nextruntest")
sys.exit(1 if failures else 0)
