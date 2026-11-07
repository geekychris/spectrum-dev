"""l2pictest - sdk l2pic: after each view Layer 2's ring holds picture
columns x..x+319 (column c in Layer 2 column c mod 320, rows 16..255), the
X scroll registers say x mod 320, and only the columns that came into view
were copied. Then the picture as an l2scroll.h band (l2pic_fill)."""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE / "tools"))
from zxdev import video  # noqa: E402
from zxdev.emu import Emulator  # noqa: E402
from gen import column  # noqa: E402

failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


PIC = [bytes(column(x)[16:]) for x in range(1000)]
# (view, the columns it should copy), as main.c's list; the last after l2pic_reset()
EXPECT = [(0, 320), (5, 5), (100, 95), (419, 319), (425, 6), (420, 5), (300, 120), (100, 200), (0, 100),
          (680, 320), (361, 319), (680, 319), (-20, 320), (680, 320)]
with Emulator(machine="next") as e:
    e.load(HERE / "build" / "l2pictest.nex")
    for x, want in EXPECT:
        cur = e.log_end()
        ok = e.run_until("lp: view", 300, since=cur)
        e.run(1)
        line = [ln for ln in e.log(cur).splitlines() if ln.startswith("lp: view")][0]
        m = re.search(r"lp: view (-?\d+) scroll (\d+) copied (\d+)", line)
        scroll, copied = int(m.group(2)), int(m.group(3))
        v = max(0, x)
        l2 = e._zone_read(0, video.NEXT_RAM + 9 * 16384, 80 * 1024)
        bad = [c for c in range(v, v + 320) if l2[(c % 320) * 256 + 16:(c % 320) * 256 + 256] != PIC[c]]
        reg = e._reg(0x16) | (e._reg(0x71) & 1) << 8
        check(f"view {x}: Layer 2 holds columns {v}..{v + 319}", ok and int(m.group(1)) == (x if "reset" not in line else 680)
              and not bad, f"{line!r}, {len(bad)} wrong, first {bad[:3]}")
        check(f"view {x}: scroll {v % 320} (the registers too)", scroll == v % 320 and reg == v % 320, f"{scroll} {reg}")
        check(f"view {x}: {want} columns copied", copied == want, str(copied))
    ok = e.run_until("lp: band 500", 100)
    e.run(1)
    off = int(re.search(r"lp: band 500 offset (\d+)", e.log(0)).group(1))
    l2 = e._zone_read(0, video.NEXT_RAM + 9 * 16384, 80 * 1024)
    bad = [c for c in range(500, 820)
           if l2[((off + c - 500) % 320) * 256 + 16:((off + c - 500) % 320) * 256 + 256] != PIC[c]]
    check("l2pic_fill as an l2scroll band: columns 500..819 in the band's ring", ok and not bad, f"{len(bad)} wrong")
    check("done", e.run_until("ZX:DONE", 100))
print(f"{'FAIL' if failures else 'PASS'}: l2pictest")
sys.exit(1 if failures else 0)
