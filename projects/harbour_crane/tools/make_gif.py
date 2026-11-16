"""docs/images/harbour_crane.gif: the title with its autopilot demo, then a
game at level 1 worked by the autopilot.

    .venv/bin/python projects/harbour_crane/tools/make_gif.py   (from the repo root)
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
    w, h = im.size                       # 352 x 304 at 2x: the 320 x 256 display at (16, 24)
    s = w // 352
    return im.crop((16 * s, 24 * s, 336 * s, 280 * s)).resize((320, 256), Image.NEAREST)


frames = []
with Emulator(machine="next") as e:
    e.load(HERE.parent / "build" / "harbour_crane.nex")
    e.run_until("hc: state TITLE", 400)
    for _ in range(45):
        e.run(EVERY)
        frames.append(grab(e))
    e.press("SPACE", 6, 6)
    e.run(20)
    e.press("B", 6, 6)
    for _ in range(170):
        e.run(EVERY)
        frames.append(grab(e))
sheet = Image.new("RGB", (320, 256 * 8))         # one palette from frames all through it
for k in range(8):
    sheet.paste(frames[k * (len(frames) - 1) // 7], (0, 256 * k))
pal = sheet.quantize(colors=64, method=Image.Quantize.MEDIANCUT)
out = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
dst = ROOT / "docs" / "images" / "harbour_crane.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=EVERY * 20, loop=0, optimize=True)
print(f"{dst}: {len(out)} frames, {dst.stat().st_size} bytes")
