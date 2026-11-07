"""A sampling profiler for Stack Tower on the Next: stops the emulator at
random T-states while the title's demo (or a game) runs and counts which
function the PC is in. For finding hot spots, not a test.

    python3 projects/stack_tower/tools/profile.py [samples] [title|play]
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

MAP = HERE.parent / "build" / "stack_tower.map"
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
        e.load(HERE.parent / "build" / "stack_tower.nex")
        e.run_until("stack: ready", 300)
        e.run(700)
        if script == "play":
            e.press("SPACE")
            e.run_until("state play", 200)
            e.run(100)
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
