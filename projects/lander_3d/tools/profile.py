"""A sampling profiler for the Next build: from the frame a log line
appears, stops the emulator at random T-states and counts the function the
PC is in (banked code resolved through the MMU). For finding hot spots, not
a test (Planet Chomp's profile.py, for this game's memory map).

    .venv/bin/python projects/lander_3d/tools/profile.py [samples] [--after TEXT] [--frames N] [--hold KEYS] [--press K@F ...]
"""
import argparse
import collections
import random
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

MAP = HERE.parent / "build" / "lander_3d.map"
syms = sorted((int(v, 16), n, f) for n, v, f in
              re.findall(r"^(\S+)\s+= \$([0-9A-F]+) ; addr, \w+, , (\w+),", MAP.read_text(), re.M))


def where(pc, mmu):
    """the function at pc: a banked symbol is (8K page of its start) << 16 | address"""
    slot = pc >> 13
    page = int(mmu[slot * 4:slot * 4 + 4], 16)
    if pc < 0x4000:
        page = int(mmu[0:4], 16)                        # 16K code banks at slot 0
    best = ("?", "?")
    for a, n, f in syms:
        bank, addr = a >> 16, a & 0xFFFF
        if addr > pc or n.startswith("__"):
            continue
        if (pc >= 0x8000 and bank == 0) or (pc < 0x8000 and bank == page):
            best = (n, f)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("samples", type=int, nargs="?", default=300)
    ap.add_argument("--after", default="ld: ready")
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--hold", default="")
    ap.add_argument("--press", action="append", default=[])
    a = ap.parse_args()
    count, files = collections.Counter(), collections.Counter()
    with Emulator(machine="next") as e:
        e.load(HERE.parent / "build" / "lander_3d.nex")
        for p in a.press:
            k, f = p.split("@")
            e.run_until(f, 3000) if not f.isdigit() else e.run(int(f))
            e.press(k, hold_frames=4)
        if a.hold:
            e.hold(*a.hold.split(","))
        e.run_until(a.after, 30000)
        start = e.position()["since_load"] + 2
        rnd = random.Random(1)
        pts = sorted((start + rnd.randrange(a.frames), rnd.randrange(0, 560000)) for _ in range(a.samples))
        for f, t in pts:
            e.run_to(f, t)
            r = e.regs()
            fn, src = where(r["PC"], r["MMU"])
            count[fn] += 1
            files[src] += 1
    total = sum(count.values())
    for fn, c in count.most_common(30):
        print(f"{100 * c / total:5.1f}%  {fn}")
    print("--")
    for f, c in files.most_common(12):
        print(f"{100 * c / total:5.1f}%  {f}")


if __name__ == "__main__":
    main()
