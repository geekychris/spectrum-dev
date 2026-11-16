"""A sampling profiler: stops the emulator at random T-states while the
test's bot plays and counts which function the PC is in. For finding hot
spots, not a test.

    .venv/bin/python projects/dot_chase/tools/profile.py [samples]
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

sys.argv = sys.argv[:2]
_src = (HERE.parent / "test.py").read_text()
_bot = {"__file__": str(HERE.parent / "test.py")}
exec(compile(_src[:_src.index("SHOTS.mkdir")], "test.py", "exec"), _bot)   # read(), choose(), KEYS

MAP = HERE.parent / "build" / "dot_chase.map"
syms = sorted((int(v, 16), n) for n, v in re.findall(r"^(\S+)\s+= \$([0-9A-F]+) ; addr", MAP.read_text(), re.M))


def where(pc):
    best = "?"
    for a, n in syms:
        if a > pc:
            break
        if not (n.startswith("l_") or n.startswith("__")):
            best = n
    return best


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    count = collections.Counter()
    with Emulator() as e:
        e.load(HERE.parent / "build" / "dot_chase.tap")
        e.run_until("dot: ready", 3000)
        e.run(30)
        e.press("SPACE")
        e.run_until("state=playing", 300)
        held = None

        def steer():
            nonlocal held
            maze, p = _bot["read"](e)
            if p[3] == 2:
                k = _bot["KEYS"][_bot["choose"](maze, p, False)]
                if k != held:
                    e.release()
                    e.hold(k)
                    held = k
        for _ in range(300):                     # until the spiders are out
            steer()
            e.run(1)
        start = e.position()["since_load"] + 1
        rnd = random.Random(1)
        for i in range(n):
            steer()
            e.run_to(start + i, rnd.randrange(0, 69888))
            count[where(e.regs()["PC"])] += 1
    for name, k in count.most_common(30):
        print(f"{100 * k / n:5.1f}% {name}")


if __name__ == "__main__":
    main()
