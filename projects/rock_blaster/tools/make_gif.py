"""docs/images/rock_blaster.gif: the title, then the test's bot playing
(aiming at the nearest rock, firing).

    .venv/bin/python projects/rock_blaster/tools/make_gif.py   (from the repo root)
"""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE))
from zxdev.emu import Emulator  # noqa: E402
from bot import step  # noqa: E402

EVERY = 2

frames = []
with Emulator() as e:
    e.load(HERE.parent / "build" / "rock_blaster.tap")
    e.run_until("rb: ready", 4000)
    for _ in range(40):
        e.run(EVERY)
        frames.append(e.screenshot().convert("RGB"))
    e.press("SPACE")
    e.run_until("rb: state=playing", 100)
    for t in range(0, 800, EVERY):
        if t % 4 == 0:
            step(e, frames=0)
        e.run(EVERY)
        frames.append(e.screenshot().convert("RGB"))
pal = frames[-1].quantize(colors=16)
out = [f.quantize(palette=pal, dither=0) for f in frames]
dst = ROOT / "docs" / "images" / "rock_blaster.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=1000 * EVERY // 50, loop=0, optimize=True)
print(dst, len(out), "frames", dst.stat().st_size // 1024, "KB")
