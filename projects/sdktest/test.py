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


for machine in ("48k", "128k"):
    print(f"== {machine}")
    with Emulator(machine=machine) as e:
        e.load(TAP)
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
