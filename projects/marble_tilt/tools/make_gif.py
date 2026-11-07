"""docs/images/marble_tilt.gif: the title with the autopilot rolling behind
it, then a game: the keys tilt the board, the autopilot rolls level 1 to the
exit.

    .venv/bin/python projects/marble_tilt/tools/make_gif.py   (from the repo root)
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
    e.load(HERE.parent / "build" / "marble_tilt.nex")
    e.run_until("mt: state TITLE", 300)
    e.run(100)
    film(e, frames, 27)
    e.press("SPACE")
    film(e, frames, 10)
    for keys in ("A", "P", "Q", "O A"):          # tilt it about by hand
        e.hold(keys)
        film(e, frames, 5)
        e.release()
        film(e, frames, 2)
    e.press("B")                                 # the autopilot to the exit
    e.log()
    while "mt: ev EXIT" not in e.log(0) and len(frames) < 300:
        film(e, frames, 1)
    film(e, frames, 25)
sheet = Image.new("RGB", (320, 256 * 8))         # one palette from frames all through it
for k in range(8):
    sheet.paste(frames[k * (len(frames) - 1) // 7], (0, 256 * k))
pal = sheet.quantize(colors=96, method=Image.Quantize.MEDIANCUT)
out = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
dst = ROOT / "docs" / "images" / "marble_tilt.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=EVERY * 20, loop=0, optimize=True)
print(f"{dst}: {len(out)} frames, {dst.stat().st_size} bytes")
