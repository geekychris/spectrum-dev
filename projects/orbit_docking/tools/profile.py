"""A sampling profiler for the Next build: stops the emulator at random
T-states while the autopilot flies and counts which function the PC is
in (banked code resolved through the MMU). For finding hot spots, not a test.

    .venv/bin/python projects/orbit_docking/tools/profile.py [samples]
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

MAP = HERE.parent / "build" / "orbit_docking.map"
syms = sorted((int(v, 16), n, f) for n, v, f in
              re.findall(r"^(\S+)\s+= \$([0-9A-F]+) ; addr, \w+, , (\w+),", MAP.read_text(), re.M))


def where(pc, mmu):
    """the function at pc: a banked symbol is (8K page of its start) << 16 | address"""
    slot = 0 if pc < 0x4000 else pc >> 13          # code banks are 16K at slot 0
    page = int(mmu[slot * 4:slot * 4 + 4], 16)
    best = ("?", "?")
    for a, n, f in syms:
        bank, addr = a >> 16, a & 0xFFFF
        if addr > pc or n.startswith("__"):
            continue
        if (pc >= 0x8000 and bank == 0) or (pc < 0x8000 and bank == page):
            best = (n, f)
    return best


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    count = collections.Counter()
    files = collections.Counter()
    with Emulator(machine="next") as e:
        e.load(HERE.parent / "build" / "orbit_docking.nex")
        e.run_until("od: ready", 500)
        e.run(100)
        e.press("SPACE", hold_frames=3, release_frames=3)      # a game, the autopilot flying it
        e.run(60)
        e.press("B", hold_frames=4, release_frames=3)
        e.run(300)
        start = e.position()["since_load"] + 20
        rnd = random.Random(1)
        for i in range(n):
            e.run_to(start + i, rnd.randrange(0, 560000))
            r = e.regs()
            fn, f = where(r["PC"], r["MMU"])
            count[fn] += 1
            files[f] += 1
    for name, k in files.most_common(25):
        print(f"{100 * k / n:5.1f}% {name}")
    print()
    for name, k in count.most_common(30):
        print(f"{100 * k / n:5.1f}% {name}")


if __name__ == "__main__":
    main()
