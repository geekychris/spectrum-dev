"""Statistical profile of the demo, per cavern: stops at a pseudo-random
T-state in each of N frames, reads the PC and charges it to the function
(nearest symbol below it in the map). python3 tools/profile.py [caverns] [-n N]"""
import argparse
import bisect
import random
import re
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("caverns", nargs="*", default=["1", "3", "6"])
ap.add_argument("-n", type=int, default=300)
a = ap.parse_args()

syms = []
for line in (HERE / "build" / "miner_meteor.map").read_text().splitlines():
    m = re.match(r"(\S+)\s+= \$([0-9A-F]{4}) ; addr", line)
    if m and not m.group(1).startswith(("__", "l_")):
        syms.append((int(m.group(2), 16), m.group(1)))
syms.sort()
addrs = [s[0] for s in syms]


def name(pc):
    i = bisect.bisect_right(addrs, pc) - 1
    return syms[i][1] if i >= 0 else f"${pc:04X}"


rnd = random.Random(1)
with Emulator() as e:
    e.load(HERE / "build" / "miner_meteor.tap")
    e.run_until("miner: demo start", 2000)
    for lv in a.caverns:
        e.run_until(f"state=playing level={lv}", 4000)
        hits = Counter()
        f0 = e.position()["since_load"] + 2
        for k in range(a.n):
            e.run_to(f0 + k, rnd.randrange(0, 69000))
            hits[name(e.regs()["PC"])] += 1
        print(f"cavern {lv}: " + ", ".join(f"{n} {100 * c // a.n}%" for n, c in hits.most_common(12)))
