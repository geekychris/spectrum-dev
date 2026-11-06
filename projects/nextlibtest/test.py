"""Next library modules (./zx test nextlibtest): DMA, copper, 320 x 256 Layer 2
and column-RLE pieces, sprite groups and masking, tilemap text, palette
effects, the TurboSound stream player and the isometric sort (the host-side
tools are sdk/tools/test_tools.py, also in ./zx selftest)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

NEX = Path(__file__).resolve().parent / "build" / "nextlibtest.nex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def near(a, b, tol=40):
    return all(abs(p - q) <= tol for p, q in zip(a, b))


with Emulator(machine="next") as e:
    e.load(NEX)
    check("runs", e.run_until("lib: done", 500))
    log = e.log(0)

    def line(tag):
        m = re.search(rf"lib: {tag}[^\n]*", log)
        return m.group(0) if m else ""

    check("DMA copy / fill", "dma copy=1 fill=1 edge=1" in log, line("dma"))
    # background 4, column 0 rows 1-2 = 1, column 1 = 2, column 3 rows 0 + 4-5 = 3 (row 3 untouched),
    # the LUT'd copy at x 318 (+0x10), columns past 320 dropped
    check("Layer 2 320x256 RLE blit, LUT, clipping", "l2w 4 1 2 3 3 4 17 18" in log, line("l2w "))
    check("Layer 2 fill + span and/or", "fill=5,4 span=132,4" in log, line("l2w fill"))
    check("palette load, second palette at half, pal_fx", "l2[5]=e0 second=60 fx=197" in log, line("pal"))
    cops = re.findall(r"cop (\d) at (\d+)", log)
    want = {"1": 284, "2": 68, "3": 96}
    check("copper waits fire on their lines (restart at line 248)",
          len(cops) == 3 and all(abs(int(l) - want[v]) <= 1 for v, l in cops), str(cops))
    check("sprite group masks (piece + rect)", "mask 227 2 227 227 2" in log, line("mask"))
    # piece column 1 (rows 0-5) lands on group column 0, rows 13..18 (row 19 untouched); column 3's
    # first run on group column 2, row 13; column 0 (rows 1-2) is off the left
    check("sprite mask clipped, across a pattern row", "mask2 227 227 227 227 2 227" in log, line("mask2"))
    check("sprite group uses 4 sprites", "next sprite=4" in log)
    check("TurboSound stream: registers", "ay r0=35 r1=1 r8=12 busy=1" in log, line("ay r0"))
    check("TurboSound stream: attenuation", "ay atten r8=8" in log, line("ay atten"))
    check("TurboSound stream: wait + end", "ay end busy=0 r8=0" in log, line("ay end"))
    check("isometric sort", "iso 1 0 2 behind=1" in log, line("iso"))

    e.run(3)
    im = e.screenshot(border=True)
    w, h = im.size
    print(f"  (screenshot {w} x {h})")
    # the composite is 352 x 304 (the 320 x 256 Layer 2 is at 16, 24): convert
    def at(x, y):
        return im.convert("RGB").getpixel((x * w // 352 + 1, y * h // 304 + 1))
    ox, oy = 16, 24
    check("copper: first palette above row 128", near(at(ox + 150, oy + 100), (36, 73, 109)), str(at(ox + 150, oy + 100)))
    check("copper: second (half) palette below", near(at(ox + 150, oy + 200), (0, 36, 36)), str(at(ox + 150, oy + 200)))
    check("sprite group drawn (grey)", near(at(ox + 220, oy + 110), (146, 146, 146)), str(at(ox + 220, oy + 110)))
    check("sprite group masked corner shows Layer 2", near(at(ox + 200, oy + 101), (36, 73, 109)), str(at(ox + 200, oy + 101)))
    check("tilemap glyph in red", any(near(at(ox + 80 + dx, oy + 32 + dy), (219, 0, 0), 50) for dx in range(16) for dy in range(16)))

print(f"{'FAIL' if failures else 'PASS'}: nextlibtest")
sys.exit(1 if failures else 0)
