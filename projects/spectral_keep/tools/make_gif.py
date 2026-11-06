"""docs/images/spectral_keep.gif: the title, then the test's bot playing
into the Great Hall to the relic and its first meeting with the ghost.

    .venv/bin/python projects/spectral_keep/tools/make_gif.py   (from the repo root)
"""
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE))
from zxdev.emu import Emulator  # noqa: E402
from bot import B_A, B_DOWN, B_LEFT, B_RIGHT, B_UP, PROBE_SIZE, Bot, decode  # noqa: E402

KEYS = [(B_UP, "Q"), (B_DOWN, "A"), (B_LEFT, "O"), (B_RIGHT, "P"), (B_A, "SPACE")]
EVERY = 4


def grab(e):
    im = e.screenshot(border=True).convert("RGB")
    w, h = im.size                       # 352 x 304 at 2x: the 320 x 256 picture at (16, 24)
    s = w // 352
    return im.crop((16 * s, 24 * s, 336 * s, 280 * s)).resize((320, 256), Image.NEAREST)


frames = []
with Emulator(machine="next") as e:
    e.load(HERE.parent / "build" / "spectral_keep.nex")
    e.run_until("keep: room drawn gate", 600)
    for _ in range(30):
        e.run(EVERY)
        frames.append(grab(e))
    bot, down, t = Bot(), "", 0
    while t < 1400:
        p = decode(e.read_mem("keep_probe", PROBE_SIZE))
        held, _ = bot.step(p)
        keys = " ".join(k for bit, k in KEYS if held & bit)
        if keys != down:
            e.release()
            if keys:
                e.hold(keys)
            down = keys
        e.run(1)
        t += 1
        if t % EVERY == 0:
            frames.append(grab(e))
        if "keep: death" in e.log(0):
            for _ in range(15):
                e.run(EVERY)
                frames.append(grab(e))
            break
sheet = Image.new("RGB", (320, 256 * 8))         # one palette from frames all through it
for k in range(8):
    sheet.paste(frames[k * (len(frames) - 1) // 7], (0, 256 * k))
pal = sheet.quantize(colors=160, method=Image.Quantize.MEDIANCUT)
out = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
dst = ROOT / "docs" / "images" / "spectral_keep.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=EVERY * 20, loop=0, optimize=True)
print(f"{dst}: {len(out)} frames, {dst.stat().st_size} bytes")
