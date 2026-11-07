"""SDK geometry modules (./zx test geomtest): l2poly's polygons, spans, bands
and rectangles on the Layer 2 back buffer (clipping, edges taller than 255
rows, edges that start above the view, both windings, DMA and unrolled
spans), and persp's projection."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

NEX = Path(__file__).resolve().parent / "build" / "geomtest.nex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


with Emulator(machine="next") as e:
    e.load(NEX)
    check("runs", e.run_until("geo: done", 500))
    log = e.log(0)

    def has(text):
        m = re.search(r"geo: " + text.split()[0] + r"[^\n]*", log)
        check(text, text in log, m.group(0) if m else "missing")

    has("tri r10=10..50 r30=10..30 r49=10..11 r50=0")       # rows ytop..ybot-1, floor(x) both ends
    has("clip r59=0 r60=240..247 r69=240 r70=0")              # l2p_top/bot/xmin/xmax, huge coordinates
    has("tall r0=100..140 r191=100..140")                     # dy 1000: the division path
    has("slant r0=100..140 r99=199..239")                     # edges starting above row 0
    has("cw r120=20..60 r159=20..60 r160=0")                  # the other winding
    has("span 256 5..9 256 0 0")                              # DMA, unrolled, 256, empty
    has("band r99=0 r100=100..156 199 r139=61..195 121 r140=0")  # rails + spans in one row loop
    has("band2 r10=60..196 r29=98..158")                      # the far edge below the near one
    has("rect 6 6 -1")                                        # clipped rectangle
    # persp: scale = fov * 4096 / z, (v * s) >> 12 floored, points offset, clamped to +-16000
    has("persp s=589 m=143,-144 130,73 145,80 -4184,-2803")
    has("scale %d %d %d" % (0x12345 // 7, 0x12345 // 300, 65535))   # 16- and 24-round divisions

print(f"{'FAIL' if failures else 'PASS'}: geomtest")
sys.exit(1 if failures else 0)
