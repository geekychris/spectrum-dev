"""A sampling profiler for the Next build: stops the emulator at random
T-states while the game plays and counts which function the PC is in
(resolving banked code by the MMU). For finding hot spots, not a test.

    .venv/bin/python projects/spectral_keep/tools/profile.py [samples] [script]

script: "walk" (the gate room, then the hall) or "title".
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

MAP = HERE.parent / "build" / "spectral_keep.map"
syms = sorted((int(v, 16), n) for n, v in re.findall(r"^(\S+)\s+= \$([0-9A-F]+) ; addr", MAP.read_text(), re.M))


def where(pc, mmu):
    """the function at pc: a banked symbol is (8K page of its start) << 16 | address"""
    slot = 0 if pc < 0x4000 else pc >> 13          # code banks are 16K at slot 0
    page = int(mmu[slot * 4:slot * 4 + 4], 16)
    best = "?"
    for a, n in syms:
        bank, addr = a >> 16, a & 0xFFFF
        if addr > pc or n.startswith("l_") or n.startswith("__"):
            continue
        if (pc >= 0x8000 and bank == 0) or (pc < 0x8000 and bank == page):
            best = n
    return best


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    script = sys.argv[2] if len(sys.argv) > 2 else "walk"
    count = collections.Counter()
    with Emulator(machine="next") as e:
        e.load(HERE.parent / "build" / "spectral_keep.nex")
        e.run_until("room drawn", 500)
        if script == "walk":
            e.run(30)
            e.press("SPACE")
            e.run_until("room drawn", 300)
            e.hold("Q O")
        start = e.position()["since_load"] + 20
        rnd = random.Random(1)
        for i in range(n):
            e.run_to(start + i, rnd.randrange(0, 560000))
            r = e.regs()
            count[where(r["PC"], r["MMU"])] += 1
    for name, k in count.most_common(30):
        print(f"{100 * k / n:5.1f}% {name}")


if __name__ == "__main__":
    main()
