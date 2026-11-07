"""Statistical profile: stops at a pseudo-random T-state in each of N frames
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
    m = re.match(r"(\S+)\s+= \$([0-9A-F]{4}) ; addr", line)
    if m and not m.group(1).startswith("__"):
        syms.append((int(m.group(2), 16), m.group(1)))
syms.sort()
addrs = [s[0] for s in syms]


def name(pc):
    i = bisect.bisect_right(addrs, pc) - 1
    return syms[i][1] if i >= 0 else f"${pc:04X}"


rnd = random.Random(1)
with Emulator() as e:
    e.load(HERE / "build" / "ace_pilot.nex")
    e.run_until(a.u, 5000)
    if a.press:
        e.press(a.press)
    e.run(a.w)
    hits = Counter()
    f0 = e.position()["since_load"] + 2
    for k in range(a.n):
        e.run_to(f0 + k, rnd.randrange(0, 550000))
        hits[name(e.regs()["PC"])] += 1
    print(", ".join(f"{n} {100 * c // a.n}%" for n, c in hits.most_common(25)))
