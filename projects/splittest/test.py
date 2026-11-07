"""Split screens and two-player input (./zx test splittest): sdk nextsplit
(copper panes with their own sprite clip windows, a palette switch on an
exact row), nextspr's sprg_show_ex (zoom, palette offset) and sprg_hide,
palfx_load_bytes and the copper list sent by DMA, and input2 (each player
reads only their own keys and joystick)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

NEX = Path(__file__).resolve().parent / "build" / "splittest.nex"
failures = []
BLUE, GREEN, RED, YELLOW = (0, 0, 255), (0, 255, 0), (255, 0, 0), (255, 255, 0)


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


with Emulator(machine="next") as e:
    e.load(NEX)
    check("runs", e.run_until("lib: ready", 300))
    e.run(3)
    img = e.screenshot()

    def px(x, y):
        """a pixel of the 320 x 256 display (ZEsarUX draws the Next at 2x, the display from 32, 48)"""
        return img.getpixel((32 + 2 * x + 1, 48 + 2 * y + 1))

    def is_(c, want):
        return all(abs(a - b) < 40 for a, b in zip(c, want))

    # split_row(200) waits for the horizontal blank at the end of row 199's line: the Next shows the
    # change from row 200; ZEsarUX draws a line after its copper moves, so it shows it from row 199
    check("Layer 2: the second palette from split_row(200) (ZEsarUX: from row 199)",
          is_(px(10, 198), BLUE) and is_(px(10, 199), GREEN) and is_(px(300, 198), BLUE) and is_(px(0, 200), GREEN)
          and is_(px(10, 100), BLUE), f"{px(10, 198)} {px(10, 199)}")
    col = [px(45, y) for y in range(110, 130)]
    want = [BLUE, BLUE] + [RED] * 8 + [BLUE] + [RED] * 7 + [BLUE] * 2   # 110..129: sprite rows 112..127
    check("pane clip windows: a sprite across the split is cut on row 120 only",
          all(is_(c, w) for c, w in zip(col, want)), str([c for c in col]))
    check("sprg_show_ex: zoom x2, palette offset 1",
          is_(px(100, 40), YELLOW) and is_(px(163, 103), YELLOW) and is_(px(164, 70), BLUE) and is_(px(130, 104), BLUE),
          f"{px(100, 40)} {px(163, 103)} {px(164, 70)} {px(130, 104)}")
    check("sprg_hide", is_(px(205, 45), BLUE) and is_(px(225, 45), BLUE), str(px(205, 45)))

    def keys_log(action):
        mark = len(e.log(0))
        action()
        e.run(4)
        m = re.findall(r"in2 p1=(\d+) p2=(\d+)", e.log(0)[mark:])
        return [int(v) for v in m[-1]] if m else None

    UP, LEFT, RIGHT, FIRE = 4, 1, 2, 16
    check("P1's W is only P1's up", keys_log(lambda: e.hold("W")) == [UP, 0])
    check("P2's I adds only P2's up", keys_log(lambda: e.hold("I")) == [UP, UP])
    check("P2's L: P2 right", keys_log(lambda: e.hold("L")) == [UP, UP | RIGHT])
    e.release()
    e.run(4)
    e.joystick("Sinclair 1")
    check("a Sinclair 1 joystick is P2's", keys_log(lambda: e.hold("LEFT+FIRE")) == [0, LEFT | FIRE])
    e.release()
    e.joystick("Sinclair 2")
    check("a Sinclair 2 joystick is P1's", keys_log(lambda: e.hold("RIGHT")) == [RIGHT, 0])
    e.release()
    e.joystick("Kempston")
    check("a Kempston joystick is P1's", keys_log(lambda: e.hold("UP+FIRE")) == [UP | FIRE, 0])
    e.release()
    e.run(4)
    e.press("ENTER")
    check("ENTER (IN2_ENTER) ends it", e.run_until("lib: done", 50))

print(f"{'FAIL' if failures else 'PASS'}: splittest")
sys.exit(1 if failures else 0)
