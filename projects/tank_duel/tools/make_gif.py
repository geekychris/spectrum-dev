"""docs/images/tank_duel.gif: the title (the autopilot fighting behind it),
then a game played by the game's own autopilot (B).

    .venv/bin/python projects/tank_duel/tools/make_gif.py   (from the repo root)
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

EVERY = 6
frames = []


def grab(e):
    frames.append(e.screenshot().convert("RGB"))


with Emulator(machine="next") as e:
    e.load(HERE.parent / "build" / "tank_duel.nex")
    e.run_until("td: ready", 3000)
    for _ in range(15):
        e.run(EVERY)
        grab(e)
    e.press("SPACE")
    e.run_until("td: start", 200)
    e.press("B")
    for _ in range(100):
        e.run(EVERY)
        grab(e)
pal = frames[len(frames) // 2].quantize(colors=32)
out = [f.quantize(palette=pal, dither=0) for f in frames]
dst = ROOT / "docs" / "images" / "tank_duel.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=1000 * EVERY // 50, loop=0, optimize=True)
print(dst, len(out), "frames", dst.stat().st_size // 1024, "KB")
