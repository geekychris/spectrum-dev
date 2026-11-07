"""SDK geometry modules (./zx test geomtest): l2poly's polygons, spans, bands
and rectangles on the Layer 2 back buffer (clipping, edges taller than 255
rows, edges that start above the view, both windings, DMA and unrolled
spans), persp's projection and l2line's lines."""
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
    has("band3 100..255 100..255 left13=0")                   # unused rails skipped: as the two used
    has("rect 6 6 -1")                                        # clipped rectangle
    # persp: scale = fov * 4096 / z, (v * s) >> 12 floored, points offset, clamped to +-16000
    # l2line: the same Bresenham and clipping in Python
    def tdiv(a, b):
        q = abs(a) // abs(b)
        return q if (a < 0) == (b < 0) else -q
    def code(x, y):
        return (1 if x < 0 else 2 if x > 255 else 0) | (4 if y < 0 else 8 if y > 191 else 0)
    def line_px(x0, y0, x1, y1):
        c0, c1 = code(x0, y0), code(x1, y1)
        while c0 | c1:
            if c0 & c1:
                return []
            c = c0 or c1
            if c & 4: x, y = x0 + tdiv((x1 - x0) * (0 - y0), y1 - y0), 0
            elif c & 8: x, y = x0 + tdiv((x1 - x0) * (191 - y0), y1 - y0), 191
            elif c & 1: x, y = 0, y0 + tdiv((y1 - y0) * (0 - x0), x1 - x0)
            else: x, y = 255, y0 + tdiv((y1 - y0) * (255 - x0), x1 - x0)
            if c == c0: x0, y0, c0 = x, y, code(x, y)
            else: x1, y1, c1 = x, y, code(x, y)
        if y1 < y0:
            x0, y0, x1, y1 = x1, y1, x0, y0
        adx, ady, sx = abs(x1 - x0), y1 - y0, 1 if x1 >= x0 else -1
        major, minor = (adx, ady) if adx >= ady else (ady, adx)
        err, x, y, out = major >> 1, x0, y0, []
        for _ in range(major + 1):
            out.append((x, y))
            err -= minor
            if adx >= ady:
                x += sx
                if err < 0: err += major; y += 1
            else:
                y += 1
                if err < 0: err += major; x += sx
        return out
    LN = [(128, 96, 250, 100), (128, 96, 200, 191), (128, 96, 130, 0), (128, 96, 0, 120), (128, 96, 20, 10),
          (128, 96, 129, 191), (-50, -20, 300, 210), (300, 50, -40, 60), (10, 300, 40, -100), (5, 5, 5, 5),
          (255, 0, 0, 191), (-10, -10, -1, 50)]
    pts = set(p for l in LN for p in line_px(*l))
    has("lines n=%d sum=%d" % (len(pts), sum((x + 1) * (y + 1) for x, y in pts)))
    has("persp s=589 m=143,-144 130,73 145,80 -4184,-2803")
    has("scale %d %d %d" % (0x12345 // 7, 0x12345 // 300, 65535))   # 16- and 24-round divisions
    # persp_xs / persp_ys match the one-point calls; persp_mul floors and saturates at +-32767
    # (-32768 fits), over signs, 4096 boundaries and the 16-bit extremes
    vs = [0, 1, -1, 7, -7, 4095, -4095, 4097, -4097, 1000, -1000, 16000, -16000, 32767, -32767, -32768, 300, -301]
    ss = [1, 589, 4096, 4097, 20000, 32767]
    mul = lambda v, s: max(-32767, min(32767, (v * s) >> 12)) if (v * s) >> 12 != -32768 else -32768
    total = sum(mul(v, s) * (i + 1) * (j + 1) for j, s in enumerate(ss) for i, v in enumerate(vs))
    has("persp batch bad=0 mulsum=%d" % total)

print(f"{'FAIL' if failures else 'PASS'}: geomtest")
sys.exit(1 if failures else 0)
