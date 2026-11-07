"""docs/images/warden_keep.gif: the title, then the test's route: a barrel
pushed, the three jewels, the room won.

    .venv/bin/python projects/warden_keep/tools/make_gif.py   (from the repo root)
"""
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

EVERY = 4


def grab(e):
    im = e.screenshot(border=True).convert("RGB")
    s = im.size[0] // 352                # 352 x 304 at 2x: the 320 x 256 picture at (16, 24)
    return im.crop((16 * s, 24 * s, 336 * s, 280 * s)).resize((320, 256), Image.NEAREST)


def film(e, frames, n):
    for _ in range(n):
        e.run(EVERY)
        frames.append(grab(e))


frames = []
with Emulator(machine="next") as e:
    e.load(HERE.parent / "build" / "warden_keep.nex")
    e.run_until("wk: state TITLE", 300)
    film(e, frames, 20)
    e.press("SPACE")
    film(e, frames, 4)
    for k in ["Q", "A", "O", "P", "P", "P", "P", "Q", "Q", "Q"]:
        e.press(k)
        film(e, frames, 3)
    film(e, frames, 25)
pal = frames[len(frames) // 2].quantize(colors=96, method=Image.Quantize.MEDIANCUT)
out = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
dst = ROOT / "docs" / "images" / "warden_keep.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=EVERY * 20, loop=0, optimize=True)
print(f"{dst}: {len(out)} frames, {dst.stat().st_size} bytes")
