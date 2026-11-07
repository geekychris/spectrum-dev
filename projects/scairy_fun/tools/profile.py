"""Statistical profiler: run a .nex, sample the PC once a frame, map it to
functions with the .map (8K pages in MMU slots 0-3 are told apart by the
bank sections' link addresses). Usage: profile.py NEX [frames] [skip]"""
import bisect
import collections
import random
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

nex = Path(sys.argv[1])
frames = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
skip = int(sys.argv[3]) if len(sys.argv) > 3 else 50
syms = {}                                   # page or None -> sorted [(addr, name)]
for line in open(nex.with_suffix(".map")):
    m = re.match(r"(\S+)\s+= \$([0-9A-F]+) ; addr, \w+, , (\S+), (\S+),", line)
    if not m:
        continue
    a = int(m.group(2), 16)
    page = a >> 16 if a > 0xFFFF else None
    syms.setdefault(page, []).append((a & 0xFFFF, m.group(1)))
for v in syms.values():
    v.sort()


def name(pc, mmu):
    page = mmu[pc >> 13] if pc < 0x8000 else None
    # a 16K bank links at its first page's number
    table = syms.get(page) or syms.get(page - 1 if page else None) or syms.get(None)
    i = bisect.bisect_right([a for a, _ in table], pc) - 1
    return table[i][1] if i >= 0 else hex(pc)


count = collections.Counter()
with Emulator(machine="next") as e:
    e.load(nex)
    e.run(20)
    if "--skip" in sys.argv:
        e.write_mem("render_skip", bytes([int(sys.argv[sys.argv.index("--skip") + 1])]))
    e.run(skip)
    rnd = random.Random(1)
    for k in range(frames):
        # a random T-state in each frame, so the samples don't lock to the frame
        e.run_to(skip + k + 1, rnd.randrange(1000, 400000))
        pc = e.regs()["PC"]
        mmu = [e._reg(0x50 + k) for k in range(4)]
        count[name(pc, mmu)] += 1
if "--raw" in sys.argv:
    for n, c in count.most_common():
        print(f"{100 * c / frames:.2f}% {n}")
    sys.exit(0)
funcs = collections.Counter()
for n, c in count.items():
    m = re.match(r"l_(\w+?)_\d+$", n)
    funcs[m.group(1) if m else n.lstrip("_")] += c
for n, c in funcs.most_common(30):
    print(f"{100 * c / frames:5.1f}% {n}")
