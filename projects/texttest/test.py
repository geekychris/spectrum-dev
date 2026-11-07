"""tmtext (./zx test texttest): text slots on the tilemap, rewritten only when
they change, and big letters from a tile pool (sdk/include/tmtext.h)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

NEX = Path(__file__).resolve().parent / "build" / "texttest.nex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


with Emulator(machine="next") as e:
    e.load(NEX)
    check("runs", e.run_until("txt: done", 500))
    log = e.log(0)
    m = re.search(r"txt: say [^\n]*", log)
    line = m.group(0) if m else ""
    check("a slot prints its text (tile 20 + 'A' - 32)", line.startswith("txt: say 53 53"), line)
    check("an unchanged slot isn't rewritten", "same=0" in line, line)
    check("a shorter text erases the old cells", "shrunk=0" in line, line)
    check("big letters: 2 x 2 tiles from the pool", "big=150,153" in line, line)
    check("TMT_CENTRE centres", "centre=53" in line, line)
    check("tmt_num", "num=-1234567" in line, line)
    e.run(2)
    im = e.screenshot().convert("RGB")
    sc = im.width // 352
    red = [(x, y) for x in range(4 * 8, 6 * 8) for y in range(20 * 8, 22 * 8)
           if im.getpixel(((16 + x) * sc, (24 + y) * sc))[0] > 180 and im.getpixel(((16 + x) * sc, (24 + y) * sc))[1] < 60]
    check("the big letter is on the screen in red", len(red) > 20, str(len(red)))

print(f"{'FAIL' if failures else 'PASS'}: texttest")
sys.exit(1 if failures else 0)
