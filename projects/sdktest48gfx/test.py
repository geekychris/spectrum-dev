"""SDK 48K drawing modules (bitline, dots48, hmap48, gauge48, attrtint,
spr32_shift) and fixmath sin14 / muls / rot2d: the program logs and draws,
this checks the log against Python and the screen pixel by pixel."""
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

TAP = Path(__file__).resolve().parent / "build" / "sdktest48gfx.tap"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def bresenham(x0, y0, x1, y1):
    """bitline's pixels: major-axis steps, error starting at half the major length"""
    dx, dy = abs(x1 - x0), abs(y1 - y0)
    sx, sy = (1 if x1 >= x0 else -1), (1 if y1 >= y0 else -1)
    pts, x, y = [], x0, y0
    if dx > dy:
        err = dx // 2
        for _ in range(dx + 1):
            pts.append((x, y))
            x += sx
            err -= dy
            if err < 0:
                err += dx
                y += sy
    else:
        err = dy // 2
        for _ in range(dy + 1):
            pts.append((x, y))
            y += sy
            err -= dx
            if err < 0:
                err += dy
                x += sx
    return pts


def rot2d(angle, sx, sy, pts):
    c = round(math.cos(2 * math.pi * angle / 256) * 16384)
    s = round(math.sin(2 * math.pi * angle / 256) * 16384)
    q14 = lambda a, b: (a * b) >> 14
    kcx, ksx, kcy, ksy = q14(c, sx), q14(s, sx), q14(c, sy), q14(s, sy)
    out = []
    for x, y in pts:
        out += [(x * kcx - y * ksx + 128) >> 8, (x * ksy + y * kcy + 128) >> 8]
    return out


with Emulator() as e:
    e.load(TAP)
    check("runs to the end", e.run_until("ZX:DONE", 3000))
    e.run(2)
    log = e.log(0)
    def nums(key):
        line = re.search(rf"gfx: {key} (.*)", log).group(1)
        return [int(v) for v in re.findall(r"-?\d+", line)]

    sin = lambda a: round(math.sin(2 * math.pi * a / 256) * 16384)
    check("fix_sin14 / fix_cos14", nums("sin14") == [0, 16384, -16384, sin(32)], str(nums("sin14")))
    check("muls", nums("muls") == [7 * 205, -12 * 192, 5 * -300, 0], str(nums("muls")))
    p = [(10, 0), (0, -8), (3, 4)]
    check("rot2d: a quarter turn", nums("rot64") == rot2d(64, 256, 256, p), f"{nums('rot64')} vs {rot2d(64, 256, 256, p)}")
    check("rot2d: 45 degrees, scaled 0.8 x 0.75", nums("rot32") == rot2d(32, 205, 192, p),
          f"{nums('rot32')} vs {rot2d(32, 205, 192, p)}")

    ula = e.screenshot(layer="ula")
    def ink_at(x, y):
        r, g, b = ula.getpixel((x, y))[:3]
        return not (r > 180 and g > 180 and b > 180)       # not white paper

    diag = set(bresenham(8, 8, 40, 30)) | set(bresenham(40, 8, 8, 30))
    box = {(x, y) for x in range(8, 41) for y in range(8, 31)}
    got = {q for q in box if ink_at(*q)}
    check("line_draw: Bresenham pixels exactly", got == diag, f"{len(got ^ diag)} differ")
    check("one-pixel line", ink_at(8, 40) and not ink_at(9, 40) and not ink_at(8, 41))
    check("line_draw_open leaves the last pixel out", all(ink_at(x, 8) for x in range(50, 60)) and not ink_at(60, 8))
    check("clipped lines reach the screen edges", ink_at(0, 60) and ink_at(255, 60) and ink_at(70, 0) and ink_at(70, 191))
    check("clip box", ink_at(100, 110) and ink_at(140, 110) and not ink_at(99, 110) and not ink_at(141, 110))
    check("XOR twice erases", not any(ink_at(x, y) for x in range(150, 201) for y in range(10, 31)))
    tri = set(bresenham(160, 40, 170, 40)[:-1] + bresenham(170, 40, 160, 50)[:-1] + bresenham(160, 50, 160, 40)[:-1])
    got = {(x, y) for x in range(155, 175) for y in range(35, 55) if ink_at(x, y)}
    check("line_poly in XOR: every vertex drawn once", got == tri, f"{sorted(got ^ tri)[:6]}")
    buf = nums("buf")
    want = [0] * 32
    for x, y in bresenham(0, 0, 31, 7) + bresenham(0, 3, 31, 3):
        want[y * 4 + x // 8] |= 0x80 >> (x % 8)
    check("bitline into a buffer (own row table, clipped)", buf == want, f"{buf} vs {want}")

    check("dots_xor: on once, off twice", ink_at(42, 42) and not ink_at(40, 40) and not ink_at(41, 41))

    h = [140 + x // 16 for x in range(256)]
    h[100] = 130
    check("hmap_line: the surface", all(ink_at(x, h[x]) for x in range(256)) and not ink_at(5, 139))
    check("hmap_line joins a spike", all(ink_at(100, y) for y in range(130, 140)) and all(ink_at(101, y) for y in range(131, 147)))
    check("hmap_fill: from 3 below the surface to the bottom",
          all(ink_at(x, y) for x in (0, 37, 200, 255) for y in range(h[x] + 3, 171))
          and not ink_at(37, h[37] + 1) and not ink_at(37, 171))
    attr = e.read_mem(0x5800, 768)
    col = lambda r, c: attr[r * 32 + c] & 7
    # column 0: surface 140 (row 17); column 31: 155 (row 19); column 12: 130..146
    check("hmap_colour: sky / surface / ground cells",
          [col(r, 0) for r in range(17, 22)] == [2, 4, 4, 4, 4]
          and [col(r, 31) for r in range(17, 22)] == [1, 1, 2, 4, 4]
          and [col(r, 12) for r in range(17, 22)] == [2, 2, 4, 4, 4],
          str([[col(r, c) for r in range(17, 22)] for c in (0, 31, 12)]))

    check("gauge: frame and bar", ink_at(16, 184) and ink_at(95, 191) and ink_at(17, 186) and ink_at(46, 189)
          and not ink_at(47, 186) and not ink_at(60, 186), "")
    check("gauge: lengths and colour", "gauge len=30 max=78 half=39 big=52" in log and col(23, 4) == 2)

    check("attrtint: plain cells tinted, scenery not", nums("tint") == [(5 << 3), (6 << 3), (5 << 3), 1, 0],
          str(nums("tint")))
    check("attrtint: restored; tint_mark", nums("untint") == [0x38, 0x30, 0x38, 0], str(nums("untint")))

    shape = [0xF0, 0x0F, 0xAA, 0x01, 0x81, 0x00, 0xFF, 0x00]
    want = []
    for line in (shape[:4], shape[4:]):
        v = int.from_bytes(bytes(line), "big") >> 3
        want += list(v.to_bytes(4, "big"))
    check("spr32_shift by 3", nums("shift3") == want, f"{nums('shift3')} vs {want}")
    check("spr32_shift by 0 copies", nums("shift0") == shape[:4])

print(f"{'FAIL' if failures else 'PASS'}: sdktest48gfx")
sys.exit(1 if failures else 0)
