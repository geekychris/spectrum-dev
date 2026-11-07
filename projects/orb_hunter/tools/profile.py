"""A sampling profiler for Orb Hunter on the Next: stops the emulator at
random T-states while a game runs (the hunter shooting) and counts which
function the PC is in. For finding hot spots, not a test.

    python3 projects/orb_hunter/tools/profile.py [samples] 
"""
import collections
import random
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

MAP = HERE.parent / "build" / "orb_hunter.map"
syms = sorted((int(v, 16), n) for n, v in re.findall(r"^(\S+)\s+= \$([0-9A-F]+) ; addr", MAP.read_text(), re.M)
              if not n.startswith("__"))


def where(pc):
    best = "?"
    for a, n in syms:
        if a > pc:
            break
        best = n
    return best


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    script = sys.argv[2] if len(sys.argv) > 2 else "title"
    count, fcount = collections.Counter(), collections.Counter()
    with Emulator(machine="next") as e:
        e.load(HERE.parent / "build" / "orb_hunter.nex")
        e.run_until("orb: ready", 300)
        e.run(30)
        e.press("SPACE")
        e.run(60)
        e.hold("SPACE")                     # shooting while standing in the first room
        start = e.position()["since_load"] + 5
        rnd = random.Random(1)
        for i in range(n):
            e.run_to(start + i, rnd.randrange(0, 560000))
            pc = e.regs()["PC"]
            w = where(pc)
            count[w] += 1
            fcount[w if not w.startswith("l_") else w] += 1
    for name, k in count.most_common(40):
        print(f"{100 * k / n:5.1f}% {name}")


if __name__ == "__main__":
    main()
