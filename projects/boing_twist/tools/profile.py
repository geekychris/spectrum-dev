"""A sampling profiler for the Next build (planet_chomp's): stops the
emulator at random T-states while Expert's scramble plays (or the title) and counts which function the PC is in.

    .venv/bin/python projects/boing_twist/tools/profile.py [samples] [title]
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

MAP = HERE.parent / "build" / "boing_twist.map"
syms = sorted((int(v, 16), n, f) for n, v, f in
              re.findall(r"^(\S+)\s+= \$([0-9A-F]+) ; addr, \w+, , (\w+),", MAP.read_text(), re.M))


def where(pc, mmu):
    slot = pc >> 13
    page = int(mmu[slot * 4:slot * 4 + 4], 16)
    best = ("?", "?")
    for a, n, f in syms:
        bank, addr = a >> 16, a & 0xFFFF
        if addr > pc or n.startswith("__"):
            continue
        if (pc >= 0x8000 and bank == 0) or (pc < 0x8000 and bank in (page, page - 1)):
            best = (n, f)
    return best


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    count = collections.Counter()
    with Emulator(machine="next") as e:
        e.load(HERE.parent / "build" / "boing_twist.nex")
        e.run_until("bt: screen TITLE", 600)
        if "title" not in sys.argv:                 # Expert's 80-move scramble: moving all the time
            e.press("SPACE")
            e.run(20)
            e.press("UP")
            e.press("LEFT")
            e.run(30)
            e.press("SPACE")
            e.run_until("bt: play", 300)
        e.run(30)
        start = e.position()["since_load"] + 5
        rnd = random.Random(1)
        for i in range(n):
            e.run_to(start + i, rnd.randrange(0, 560000))
            r = e.regs()
            count[where(r["PC"], r["MMU"])[0]] += 1
        print(e.log(0)[-400:])
    for fn, c in count.most_common(25):
        print(f"{c * 100 / n:5.1f}%  {fn}")


if __name__ == "__main__":
    main()
