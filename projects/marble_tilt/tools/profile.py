"""A sampling profiler for the Next build (after spectral_keep's): stops the
emulator at random T-states and counts which function the PC is in. For
finding hot spots, not a test.

    .venv/bin/python projects/marble_tilt/tools/profile.py [samples] [log line to start at]
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

MAP = HERE.parent / "build" / "marble_tilt.map"
syms = sorted((int(v, 16), n) for n, v in re.findall(r"^(\S+)\s+= \$([0-9A-F]+) ; addr", MAP.read_text(), re.M))


def where(pc, mmu):
    """the function at pc: a banked symbol is (8K page of its start) << 16 | address"""
    slot = pc >> 13
    page = int(mmu[slot * 4:slot * 4 + 4], 16)
    best = "?"
    for a, n in syms:
        bank, addr = a >> 16, a & 0xFFFF
        if addr > pc or n.startswith("__"):
            continue
        if (pc >= 0x8000 and bank == 0) or (pc < 0x8000 and bank == page):
            best = n
    return best


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    until = sys.argv[2] if len(sys.argv) > 2 else "mt: profile"
    count = collections.Counter()
    with Emulator(machine="next") as e:
        e.load(HERE.parent / "build" / "marble_tilt.nex")
        e.run_until(until, 3000)
        start = e.position()["since_load"] + 5
        rnd = random.Random(1)
        for i in range(n):
            e.run_to(start + i, rnd.randrange(0, 560000))
            r = e.regs()
            count[where(r["PC"], r["MMU"])] += 1
    for name, k in count.most_common(28):
        print(f"{100 * k / n:5.1f}% {name}")
    # by the module (source file) each function is in
    mod = {}
    for line in MAP.read_text().splitlines():
        m = re.match(r"^(\S+)\s+= \$[0-9A-F]+ ; addr, \w+, \S*, (\S+),", line)
        if m:
            mod[m.group(1)] = m.group(2)
    by = collections.Counter()
    for name, k in count.items():
        by[mod.get(name, "?")] += k
    print("--")
    for name, k in by.most_common(20):
        print(f"{100 * k / n:5.1f}% {name}")


if __name__ == "__main__":
    main()
