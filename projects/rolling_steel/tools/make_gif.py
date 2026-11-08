"""docs/images/rolling_steel.gif: the title with the course flying by behind
it, then a game: the keys push the marble, the original's autopilot rolls
on down the course.

    .venv/bin/python projects/rolling_steel/tools/make_gif.py   (from the repo root)
"""
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

EVERY = 6


def grab(e):
    im = e.screenshot(border=True).convert("RGB")
    w, h = im.size                       # 352 x 304 at 2x: the 320 x 256 picture at (16, 24)
    s = w // 352
    return im.crop((16 * s, 24 * s, 336 * s, 280 * s)).resize((320, 256), Image.NEAREST)


def film(e, frames, n):
    for _ in range(n):
        e.run(EVERY)
        frames.append(grab(e))


frames = []
with Emulator(machine="next") as e:
    e.load(HERE.parent / "build" / "rolling_steel.nex")
    e.run_until("rs: state 0", 300)
    e.run(60)
    film(e, frames, 25)
    e.press("SPACE")
    film(e, frames, 6)
    for keys in ("Q", "Q P"):                     # push it by hand
        e.hold(keys)
        film(e, frames, 8)
        e.release()
    e.press("B")                                  # the autopilot drives
    film(e, frames, 150)
sheet = Image.new("RGB", (320, 256 * 8))          # one palette from frames all through it
for k in range(8):
    sheet.paste(frames[k * (len(frames) - 1) // 7], (0, 256 * k))
pal = sheet.quantize(colors=96, method=Image.Quantize.MEDIANCUT)
out = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
dst = ROOT / "docs" / "images" / "rolling_steel.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=EVERY * 20, loop=0, optimize=True)
print(f"{dst}: {len(out)} frames, {dst.stat().st_size} bytes")
