"""Statistical profile (banked code by the page in MMU slot 0): stops at a pseudo-random T-state in each of N frames
after a warm-up, reads the PC and charges it to the function (nearest symbol
below it in the map). python3 tools/profile.py [-w WARMUP_FRAMES] [-n N] [-u TEXT]"""
import argparse
import bisect
import random
import re
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE.parents[1] / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("-w", type=int, default=300)
ap.add_argument("-n", type=int, default=400)
ap.add_argument("-u", default="ace: ready")
ap.add_argument("--press", default="")
a = ap.parse_args()

syms = []
for line in (HERE / "build" / "ace_pilot.map").read_text().splitlines():
    m = re.match(r"(\S+)\s+= \$([0-9A-F]{4,6}) ; addr", line)
    if m and not m.group(1).startswith("__") and not m.group(1).startswith("i_"):
        syms.append((int(m.group(2), 16), m.group(1)))
syms.sort()
addrs = [s[0] for s in syms]


def name(pc, mmu=""):
    if pc < 0x4000 and mmu:                 # a bank at 0x0000: its symbols are page << 16 | address
        page = int(mmu[0:4], 16)
        if page < 0xFF:
            pc |= page << 16
    i = bisect.bisect_right(addrs, pc) - 1
    if i < 0:
        return f"${pc:04X}"
    m = re.match(r"l_(.+)_\d{5}$", syms[i][1])      # zsdcc's local labels: their function
    return "_" + m.group(1) if m else syms[i][1]


rnd = random.Random(1)
with Emulator(machine="next") as e:
    e.load(HERE / "build" / "ace_pilot.nex")
    e.run_until(a.u, 5000)
    if a.press:
        e.press(a.press)
    e.run(a.w)
    hits = Counter()
    f0 = e.position()["since_load"] + 2
    for k in range(a.n):
        e.run_to(f0 + k, rnd.randrange(0, 550000))
        r = e.regs()
        hits[name(r["PC"], r.get("MMU", ""))] += 1
    print(", ".join(f"{n} {100 * c // a.n}%" for n, c in hits.most_common(25)))
