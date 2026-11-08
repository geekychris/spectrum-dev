"""Where a frame's time goes, by section (src/prof.h; the game built with
`bin/zx-make -C projects/lander_3d PROF=1` after a clean): the title's
demo, then a game with the autopilot flying (B) and one falling with no
thrust. Prints each section's share and its raster lines (~1,800 T-states
each) a pass.

    .venv/bin/python projects/lander_3d/tools/frame_prof.py [--frames N]
"""
import argparse
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(HERE.parents[1] / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

NAMES = ["other", "rules", "hud values", "camera", "ground setup", "projection", "objects", "sky",
         "ground cols", "polygons", "text", "shadow+lander", "autopilot"]


def report(title, log):
    # (signed: a frame boundary can make a section's sum dip below 0 by a
    # few lines; the next section has them)
    rows = [[(int(v) + 32768) % 65536 - 32768 for v in m.split()] for m in re.findall(r"ld: prof ([\d ]+) loops=", log)]
    loops = sum(int(v) for v in re.findall(r"ld: prof [\d ]+ loops=(\d+)", log))
    fps = [int(v) / 10 for v in re.findall(r"perf fps10=(\d+)", log)]
    if not rows or not loops:
        print(title, "no data")
        return
    tot = [sum(r[k] for r in rows) for k in range(len(NAMES))]
    s = sum(tot)
    print(f"{title}: fps {fps}  ({s / loops:.0f} lines a pass)")
    for n, t in sorted(zip(NAMES, tot), key=lambda x: -x[1]):
        print(f"  {n:13s} {100 * t / s:5.1f}%  {t / loops:6.1f} lines")


ap = argparse.ArgumentParser()
ap.add_argument("--frames", type=int, default=1500)
a = ap.parse_args()
with Emulator(machine="next") as e:
    e.load(HERE / "build" / "lander_3d.nex")
    e.run_until("ld: state TITLE", 3000)
    e.run(a.frames)
    report("title (the demo)", e.log(0))
    e.press("FIRE")
    e.run_until("ld: level MOON 1", 100)
    e.press("B", hold_frames=4)
    mark = len(e.log(0))
    e.run(a.frames)
    report("the autopilot flying (Moon 1)", e.log(0)[mark:])
    e.run_until("ld: level MOON 2", 6000)
    e.press("B", hold_frames=4)
    mark = len(e.log(0))
    e.run_until("ld: outcome", 3000)
    report("falling, no thrust (Moon 2)", e.log(0)[mark:])
