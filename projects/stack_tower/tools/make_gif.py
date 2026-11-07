"""docs/images/stack_tower.gif: the title's demo game, then the test's bot
playing: the tower turns with each piece, rings clear.

    .venv/bin/python projects/stack_tower/tools/make_gif.py   (from the repo root)
"""
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE))
from zxdev.emu import Emulator  # noqa: E402
from bot import Bot  # noqa: E402

EVERY = 6
PIECES = 14


def grab(e):
    im = e.screenshot(border=True).convert("RGB")
    w, h = im.size                       # 352 x 304 at 2x: the 320 x 256 picture at (16, 24)
    s = w // 352
    return im.crop((16 * s, 24 * s, 336 * s, 280 * s)).resize((320, 256), Image.NEAREST)


class Filming:
    """the emulator, with a picture kept every EVERY frames it runs"""

    def __init__(self, e):
        self.e, self.t, self.frames = e, 0, []

    def run(self, n=1):
        for _ in range(n):
            self.e.run(1)
            self.t += 1
            if self.t % EVERY == 0:
                self.frames.append(grab(self.e))

    def press(self, keys):
        self.e.hold(keys)
        self.run(3)
        self.e.release()
        self.run(3)

    def read_mem(self, *a):
        return self.e.read_mem(*a)


with Emulator(machine="next") as e:
    e.load(HERE.parent / "build" / "stack_tower.nex")
    e.run_until("stack: ready", 300)
    e.run(560)                          # past the painting: the demo game
    f = Filming(e)
    f.run(120)
    f.press("SPACE")
    f.run(20)
    bot = Bot(f)
    while bot.probe()["pieces"] < PIECES:
        bot.place()
    f.run(60)
    frames = f.frames
sheet = Image.new("RGB", (320, 256 * 8))         # one palette from frames all through it
for k in range(8):
    sheet.paste(frames[k * (len(frames) - 1) // 7], (0, 256 * k))
pal = sheet.quantize(colors=32, method=Image.Quantize.MEDIANCUT)
out = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames]
dst = ROOT / "docs" / "images" / "stack_tower.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=EVERY * 20, loop=0, optimize=True)
print(f"{dst}: {len(out)} frames, {dst.stat().st_size} bytes")
