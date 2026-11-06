"""Next SDK tests (./zx test sdktestnext): Layer 2 double buffering, drawing,
palette, hardware sprites, TurboSound, common maths/timer/input on the Next."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

NEX = Path(__file__).resolve().parent / "build" / "sdktestnext.nex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


def near(a, b, tol=40):
    return all(abs(p - q) <= tol for p, q in zip(a, b))


with Emulator(machine="next") as e:
    e.load(NEX)
    check("runs", e.run_until("sdk: input ready", 3000))
    log = e.log(0)
    check("28 MHz, AY present", "speed=3 ay=1" in log, log.splitlines()[0])
    check("double buffer flips 9 -> 12", "front before flip=9" in log and "front after flip=12 back=9" in log)
    check("palette write/read", "l2 palette[1]=03" in log)
    check("TurboSound chip 1 registers", "ts chip1 r0=144" in log)
    check("rnd + fix maths on the Next", "rnd=33153,24609 fix_div=384 sin32=181" in log)
    check("frame timer at 28 MHz", "frames=25" in log)
    # chip 1 A: period 300 (low byte 44), volume 12, tone on; chip 0 B: noise at 8
    # (9 - 1 after a tick), noise B on and tone C on (the effect) in chip 0's mixer
    check("modplay: tone on chip 1, noise on chip 0, the effect's mixer bit kept",
          "modplay c1a=44,12 mix1=3e c0b=8 mix0=2b" in log, [l for l in log.splitlines() if "modplay" in l])

    l2 = e.screenshot(layer="layer2")
    check("Layer 2 border rectangle", near(l2.getpixel((0, 0)), (255, 255, 255)) and near(l2.getpixel((255, 191)), (255, 255, 255)))
    # cleared to index 1, which the program then sets to pure blue in the palette
    check("Layer 2 clear colour (palette entry 1 = blue)", near(l2.getpixel((128, 180)), (0, 0, 255)), str(l2.getpixel((128, 180))))
    check("Layer 2 fill", near(l2.getpixel((30, 40)), (255, 0, 0)))
    check("Layer 2 scaled text", sum(near(l2.getpixel((x, y)), (0, 255, 0)) for x in range(80, 144) for y in range(30, 46)) > 50)
    blue = sum(near(l2.getpixel((x, y)), (0, 255, 255)) for x in range(150, 210) for y in range(60, 74))
    check("5x7 font text (l2_text5)", 100 < blue < 500, str(blue))
    check("keyed blit keeps the background", near(l2.getpixel((21, 100)), (255, 255, 0)) and not near(l2.getpixel((20, 100)), (255, 255, 0)))
    check("plot", near(l2.getpixel((200, 150)), (255, 0, 255)))
    sp = e.screenshot(layer="sprites")
    a, b = sp.getpixel((100 + 4, 80 + 8)), sp.getpixel((200 + 4, 120 + 8))
    check("sprite 0 drawn (left half green)", a[3] == 255 and a[1] > 200 and a[0] < 60, str(a))
    check("sprite 1 mirrored (left half orange)", b[3] == 255 and b[0] > 200, str(b))
    check("sprite 2 hidden", sp.getpixel((300 + 8, 200 + 8))[3] == 0)
    full = e.screenshot()
    c = full.getpixel((96 + 2 * (100 + 4 - 32), 112 + 2 * (80 + 8 - 32)))
    check("sprite composited over Layer 2", c[1] > 200 and c[0] < 60, str(c))
    for keys, bits in (("P", "02"), ("FIRE", "10"), ("ENTER", "40")):
        e.press(keys)
        check(f"input {keys}", e.run_until(f"sdk: pressed={bits}", 10))
    check("320 x 256 mode set up", e.run_until("sdk: wide ready", 20))
    e.run(2)
    full = e.screenshot()
    P = lambda x, y: full.getpixel((32 + 2 * x, 48 + 2 * y))      # display pixel -> screenshot
    check("l2w_fill in the border area (x 290..309)", near(P(300, 210), (255, 0, 0)) and not near(P(285, 210), (255, 0, 0)), str(P(300, 210)))
    green = sum(near(P(x, y), (0, 255, 0)) for x in range(10, 60) for y in range(10, 26))
    check("l2w_text5", 60 < green < 400, str(green))
    check("quad sprite: TL TR BL BR", near(P(104, 104), (255, 0, 0)) and near(P(120, 104), (0, 255, 0))
          and near(P(104, 120), (0, 0, 255)) and near(P(120, 120), (255, 255, 0)), str([P(104, 104), P(120, 104)]))
    check("quad sprite mirrored: the halves swap", near(P(204, 104), (0, 255, 0)) and near(P(220, 104), (255, 0, 0)), str([P(204, 104), P(220, 104)]))
    check("finishes", e.run_until("ZX:DONE", 20))

print(f"{'FAIL' if failures else 'PASS'}: sdktestnext")
sys.exit(1 if failures else 0)
