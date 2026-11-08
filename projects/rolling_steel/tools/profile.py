"""A sampling profiler for the Next build: stops the emulator at random
T-states while the attract demo plays course 1 and counts which routine the
PC is in (banked code resolved through the MMU). For finding hot spots, not
a test.   .venv/bin/python projects/rolling_steel/tools/profile.py [samples]
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

MAP = HERE.parent / "build" / "rolling_steel.map"
syms = sorted((int(v, 16), n, f) for n, v, f in
              re.findall(r"^(\S+)\s+= \$([0-9A-F]+) ; addr, \w+, , (\w+),", MAP.read_text(), re.M))


def where(pc, mmu):
    """the routine at pc: a banked symbol is (8K page of its start) << 16 | address"""
    page = int(mmu[0:4], 16)
    best = ("?", "?")
    for a, n, f in syms:
        bank, addr = a >> 16, a & 0xFFFF
        if addr > pc or n.startswith("__"):
            continue
        if (pc >= 0x4000 and bank == 0) or (pc < 0x4000 and bank == page):
            best = (n, f)
    return best


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    count = collections.Counter()
    files = collections.Counter()
    with Emulator(machine="next") as e:
        e.load(HERE.parent / "build" / "rolling_steel.nex")
        e.run_until("demo starts course=1", 2000)
        e.run(100)
        start = e.position()["since_load"] + 5
        rnd = random.Random(1)
        for i in range(n):
            e.run_to(start + i, rnd.randrange(0, 560000))
            r = e.regs()
            fn, f = where(r["PC"], r["MMU"])
            count[fn] += 1
            files[f] += 1
    for name, k in files.most_common(15):
        print(f"{100 * k / n:5.1f}% {name}")
    print()
    for name, k in count.most_common(40):
        print(f"{100 * k / n:5.1f}% {name}")


if __name__ == "__main__":
    main()
