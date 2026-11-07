"""docs/images/orb_hunter.gif: the title, then the test's bot playing: out
of the start room, down into the next cavern, shooting a creature.

    .venv/bin/python projects/orb_hunter/tools/make_gif.py   (from the repo root)
"""
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE))
from zxdev.emu import Emulator  # noqa: E402
from bot import PROBE_SIZE, Bot, decode  # noqa: E402

EVERY = 4
FRAMES = 520


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
    e.load(HERE.parent / "build" / "orb_hunter.nex")
    e.run_until("orb: ready", 300)
    f = Filming(e)
    f.run(60)
    f.press("SPACE")
    bot, down = Bot(), ""
    for _ in range(FRAMES):
        keys = bot.keys(decode(e.read_mem("orb_probe", PROBE_SIZE)))
        if keys != down:
            e.release()
            if keys:
                e.hold(keys)
            down = keys
        f.run(1)
    e.release()
    frames = f.frames
sheet = Image.new("RGB", (320, 256 * 8))         # one palette from frames all through it
for k in range(8):
    sheet.paste(frames[k * (len(frames) - 1) // 7], (0, 256 * k))
pal = sheet.quantize(colors=32, method=Image.Quantize.MEDIANCUT)
out = [fr.quantize(palette=pal, dither=Image.Dither.NONE) for fr in frames]
dst = ROOT / "docs" / "images" / "orb_hunter.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=EVERY * 20, loop=0, optimize=True)
print(f"{dst}: {len(out)} frames, {dst.stat().st_size} bytes")
