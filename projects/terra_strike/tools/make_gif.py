"""docs/images/terra_strike.gif: the title (the flyover), then a flight by
the test's pilot (tools/tsbot.py): lift off, hunt the targets.

    .venv/bin/python projects/terra_strike/tools/make_gif.py   (from the repo root)
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE))
from zxdev.emu import Emulator  # noqa: E402
from tsbot import Pilot  # noqa: E402

EVERY = 8
frames = []


def grab(e):
    frames.append(e.screenshot().convert("RGB"))


with Emulator(machine="next") as e:
    e.load(HERE.parent / "build" / "terra_strike.nex")
    e.run_until("ts: ready", 3000)
    for _ in range(20):
        e.run(EVERY)
        grab(e)
    e.press("SPACE")
    e.run_until("ts: STATE PLAY", 200)
    pilot = Pilot(e)
    for _ in range(130):
        pilot.step(EVERY)
        grab(e)
pal = frames[len(frames) // 2].quantize(colors=48)
out = [f.quantize(palette=pal, dither=0) for f in frames]
dst = ROOT / "docs" / "images" / "terra_strike.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=1000 * EVERY // 50, loop=0, optimize=True)
print(dst, len(out), "frames", dst.stat().st_size // 1024, "KB")
