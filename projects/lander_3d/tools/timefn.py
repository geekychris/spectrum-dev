"""T-states each call of some functions takes (Miner Meteor's timefn.py
for the Next): from a log line, a breakpoint at a function's entry, then
at its return address; a few calls each.

    .venv/bin/python projects/lander_3d/tools/timefn.py [--after TEXT] [--hold KEYS] [--calls N] _fn1 _fn2 ...
"""
import argparse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE.parents[1] / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

FRAME = 560000                          # 28 MHz, 50 Hz


def now(e):
    p = e.position()
    return p["frame"] * FRAME + p["tstates"]


ap = argparse.ArgumentParser()
ap.add_argument("fns", nargs="+")
ap.add_argument("--after", default="ld: perf")
ap.add_argument("--hold", default="")
ap.add_argument("--calls", type=int, default=5)
ap.add_argument("--frames", type=int, default=0)
a = ap.parse_args()
with Emulator(machine="next") as e:
    e.load(HERE / "build" / "lander_3d.nex")
    if a.hold:
        e.hold(*a.hold.split(","))
    e.run_until(a.after, 30000)
    e.run(a.frames)
    for fn in a.fns:
        res = []
        for _ in range(a.calls):
            e.break_at(fn)
            if not e.continue_(400)["stopped"]:
                e.delete()
                break
            ret = int.from_bytes(e.read_mem(e.regs()["SP"], 2), "little")
            t0 = now(e)
            e.delete()
            e.break_at(ret)
            e.continue_(400)
            res.append(now(e) - t0)
            e.delete()
        print(f"{fn:22} {res}")
