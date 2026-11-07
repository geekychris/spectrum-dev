"""SDK game-layer tests on the 48K and the 128K (run by ./zx test sdktest).

The program logs what the SDK computed; this checks it against Python and
checks the screen for what it drew.
"""
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

TAP = Path(__file__).resolve().parent / "build" / "sdktest.tap"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def xorshift(s, n):
    out = []
    for _ in range(n):
        s ^= (s << 7) & 0xFFFF
        s ^= s >> 9
        s ^= (s << 8) & 0xFFFF
        out.append(s)
    return out


def val(log, key):
    m = re.search(rf"{re.escape(key)}=([-\w,]+)", log)
    return m.group(1) if m else None


# ---- a model of line48.asm (same Bresenham, same wrap) for the line tests
def trunc_div(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def wrap_row(y):
    y &= 255
    return y if y < 192 else (y - 192 if y < 224 else y - 64)


def model_line(scr, x0, y0, x1, y1, mode, skip=False, signed=False):
    def axis(a0, a1):
        raw = (a1 - a0) & 255
        neg = raw >= 128 if signed else a1 < a0
        return ((-raw) & 255 if neg else raw), (-1 if neg else 1)
    adx, sx = axis(x0, x1)
    ady, sy = axis(y0, y1)
    major, minor = (adx, ady) if adx >= ady else (ady, adx)
    n = major + 1 - (1 if skip else 0)
    err, x, y = major >> 1, x0, y0
    for _ in range(n):
        p = (x & 255, wrap_row(y))
        if mode == "xor":
            scr ^= {p}
        else:
            scr.add(p)
        err -= minor
        if adx >= ady:
            x += sx
            if err < 0:
                err += major
                y += sy
        else:
            y += sy
            if err < 0:
                err += major
                x += sx


def model_poly(scr, cx, cy, pts, closed, mode):
    v = [((cx + x) & 255, (cy + y) & 255) for x, y in pts]
    for a, b in zip(v, v[1:]):
        model_line(scr, *a, *b, mode, skip=True, signed=True)
    if closed:
        model_line(scr, *v[-1], *v[0], mode, skip=True, signed=True)
    else:
        model_line(scr, *v[-1], *v[-1], mode, signed=True)


def model_clip(scr, x0, y0, x1, y1):
    def code(x, y):
        return (1 if x < 0 else 2 if x > 255 else 0) | (4 if y < 0 else 8 if y > 191 else 0)
    c0, c1 = code(x0, y0), code(x1, y1)
    while c0 | c1:
        if c0 & c1:
            return
        c = c0 or c1
        if c & 4:
            x, y = x0 + trunc_div((x1 - x0) * (0 - y0), y1 - y0), 0
        elif c & 8:
            x, y = x0 + trunc_div((x1 - x0) * (191 - y0), y1 - y0), 191
        elif c & 1:
            x, y = 0, y0 + trunc_div((y1 - y0) * (0 - x0), x1 - x0)
        else:
            x, y = 255, y0 + trunc_div((y1 - y0) * (255 - x0), x1 - x0)
        if c == c0:
            x0, y0, c0 = x, y, code(x, y)
        else:
            x1, y1, c1 = x, y, code(x, y)
    model_line(scr, x0, y0, x1, y1, "or")


SIN = [round(math.sin(2 * math.pi * a / 256) * 256) for a in range(256)]


def model_rotate(pts, angle, scale):
    s, c = SIN[angle & 255], SIN[(angle + 64) & 255]
    return [(((x * c - y * s) * scale + 0x8000) >> 16, ((x * s + y * c) * scale + 0x8000) >> 16) for x, y in pts]


OCTAGON = [(-6, -14), (6, -16), (16, -6), (14, 3), (10, 14), (-3, 16), (-14, 8), (-16, -5)]
ARROW = [(10, 0), (-7, -6), (-4, -2), (-4, 2), (-7, 6)]
FAN = [(90, 157), (88, 170), (82, 182), (68, 187), (60, 187), (50, 186), (35, 175), (30, 160),
       (30, 157), (31, 140), (40, 130), (55, 127), (60, 127), (72, 128), (85, 136), (89, 150)]


def model_lines_screen():
    scr = set()
    for x, y in FAN:
        model_line(scr, 60, 157, x, y, "or")
    model_line(scr, 100, 125, 100, 125, "or")
    model_clip(scr, -40, 100, 300, 110)
    model_clip(scr, -10, -10, -1, 50)
    model_clip(scr, 250, 180, 270, 200)
    scr |= {(5, 5), (6, 5), (250, 190)}
    for c in ((128, 2), (190, 128), (196, 132), (190, 128), (196, 132)):
        model_poly(scr, *c, OCTAGON, True, "xor")
    model_poly(scr, 220, 60, ARROW, False, "xor")
    model_poly(scr, 104, 44, OCTAGON, True, "xor")               # poly_sync's result
    model_poly(scr, 160, 60, model_rotate(ARROW, 32, 205), True, "or")
    return scr


def check_lines(e):
    check("line48: reaches the line tests", e.run_until("sdk: lines drawn", 4000))
    ula = e.screenshot(layer="ula")
    got = {(x, y) for y in range(192) for x in range(256) if ula.getpixel((x, y)) == (0, 0, 0)}
    text = {p for p in got if 32 <= p[0] < 48 and 3 <= p[1] < 11}
    want = model_lines_screen()
    diff = (got - text) ^ want
    check("line48: every line, clip, polygon, compiled polygon, sync and point matches the model", not diff,
          f"{len(diff)} pixels differ, e.g. {sorted(diff)[:8]}")
    check("line48: wrapped polygon shows at the top and the bottom",
          any(y < 20 for x, y in want if 110 < x < 150) and any(y > 180 for x, y in want if 110 < x < 150))
    check("text_xor: twice erases, once draws", len(text) > 10 and not any(x < 24 and 3 <= y < 19 for x, y in got - want))
    log = e.log(0)
    rot = model_rotate(ARROW, 32, 205)
    exp = f"rot={rot[0][0]},{rot[0][1]},{rot[1][0]},{rot[1][1]},{rot[4][0]},{rot[4][1]}"
    check("vec_rotate matches Python", exp in log, f"{exp} vs {val(log, 'rot')}")
    check("vec_rot_table + vec_rot_at", "rottab=0,10,-10,-10 at250=0" in log, val(log, "rottab"))
    circ = [(100, 100, 10), (300, -50, 20), (-200, 80, 5), (150, 100, 8)]
    qs = [(105, 104, 2), (325, -50, 4), (320, -45, 4), (-200, 80, 0), (156, 106, 2), (1000, 1000, 5),
          (-197, 84, 0), (400, 100, 3)]
    exp = [next((k for k, (cx, cy, cr) in enumerate(circ)
                 if abs(x - cx) < r + cr and abs(y - cy) < r + cr and (x - cx) ** 2 + (y - cy) ** 2 < (r + cr) ** 2), 255)
           for x, y, r in qs]
    check("hit48: hit_first matches Python", "hit=" + ",".join(map(str, exp)) in log, f"{exp} vs {val(log, 'hit')}")
    check("line48: 50 XOR octagons", e.run_until("sdk: 50 octagons", 400))
    e.run(1)
    print("   ", re.findall(r"50 octagons frames=\d+", e.log(0)))


for machine in ("48k", "128k"):
    print(f"== {machine}")
    with Emulator(machine=machine) as e:
        e.load(TAP)
        check_lines(e)
        check("runs to the initials editor", e.run_until("sdk: hs edit ready", 4000))
        e.run(2)
        for k in "BOT":                                   # 2-frame taps (a real press is longer);
            e.press(k, hold_frames=2, release_frames=1)   # the loop reads every 3rd frame only
        check("initials editor: quick taps latched (hs_edit)", e.run_until("sdk: hs edit name=BOT", 30))
        check("runs to the input stage", e.run_until("sdk: input ready", 100))
        log = e.log(0)
        check("fix_mul / fix_div", val(log, "fix_mul") == "768" and val(log, "fix_div") == "384"
              and val(log, "fix_div2") == "-384,64,-512", log.splitlines()[0])
        sin = lambda a: round(math.sin(2 * math.pi * a / 256) * 256)
        check("sin/cos table", val(log, "sin64") == "256" and val(log, "cos0") == "256"
              and val(log, "sin192") == "-256" and val(log, "sin32") == str(sin(32)))
        check("atan2", (val(log, "atan11"), val(log, "atan-1-1"), val(log, "atan01"), val(log, "atan-10"))
              == ("32", "160", "64", "128"))
        check("isqrt16", val(log, "isqrt") == "100,255,1")
        exp = xorshift(1, 5)
        check("fixmath: mul16 / mulq14 / mulq8 / isqrt32 / div_q14", "fixmath -370200 1073741824 -65534 -5 5115 -301 65535 300 1 -12288 16384" in log, [l for l in log.splitlines() if "fixmath" in l])
        check("xorshift rnd matches Python", val(log, "rnd") == ",".join(map(str, exp[:4]))
              and val(log, "range") == str(exp[4] % 10), f"{val(log, 'rnd')} vs {exp}")
        check("high-score insert", "rank=1 top=ZXD,5000 second=BOT,4500 last=2000 low=-1" in log)
        check("gfx_point", val(log, "point") == "1,0")
        check("beeper tone length (~15 frames)", 14 <= int(val(log, "beep frames")) <= 16, val(log, "beep frames"))
        check("frame timer: 50 waits = 50 frames", val(log, "waited") == "50")
        ov = re.search(r"overruns=(\d+)\n.*overruns after a long beep=(\d+)", log, re.S)
        check("frame overruns counted (a 45-frame beep)", ov and 40 <= int(ov.group(2)) - int(ov.group(1)) <= 46,
              ov.groups() if ov else log)
        ay = val(log, "ay")
        check(f"AY {'present' if machine == '128k' else 'absent'}", ay == ("1" if machine == "128k" else "0"))
        if machine == "128k":
            check("AY tone registers", "ay r0=28 r8=12" in log)
            check("AY effect decays per sound_tick", "ay sfx vol=6" in log)
        check("Kempston detected", val(log, "kempston") == "1")

        ula = e.screenshot(layer="ula")
        px = lambda x, y: ula.getpixel((x, y)) == (0, 0, 0)     # ink (black) on white
        check("pre-shifted sprite at (13,21)", px(13, 21) and px(28, 21) and px(13, 23)
              and not px(14, 23) and not px(12, 21) and not px(29, 21))
        check("XOR twice erases", not any(px(x, y) for x in range(100, 117) for y in range(50, 54)))
        check("clipped sprite at the right edge", px(250, 60) and px(255, 60))
        check("big text drawn", sum(px(x, y) for x in range(16, 64) for y in range(80, 96)) > 100)
        ink = lambda x, y: ula.getpixel((x, y)) == (0xFF, 0xFF, 0)   # yellow ink there
        check("fill_rect pattern 0xAA", ink(160, 100) and not ink(161, 100))
        a = ula.getpixel((165, 97))
        check("attributes (bright red paper)", a == (0xFF, 0, 0), str(a))

        for keys, bits in (("O", "01"), ("SPACE", "10"), ("RIGHT+FIRE", "12"), ("Q", "04"), ("1", "01")):
            e.press(keys)
            check(f"input {keys} -> {bits}", e.run_until(f"sdk: input={bits}", 10))
        e.press("ENTER")
        check("ENTER = IN_START, finishes", e.run_until("ZX:DONE", 20))

print(f"{'FAIL' if failures else 'PASS'}: sdktest")
sys.exit(1 if failures else 0)
