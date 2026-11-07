"""docs/images/planet_chomp.gif: the title, the attract demo (the original's
autopilot) starting, then the demo taking a golden key and eating a
frightened spook (9405 steps in).

    .venv/bin/python projects/planet_chomp/tools/make_gif.py   (from the repo root)
"""
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

EVERY = 8                                   # the picture changes about every 8 frames


def grab(e):
    im = e.screenshot(border=True).convert("RGB")
    w, h = im.size                          # 352 x 304 at 2x: the 320 x 256 picture at (16, 24)
    s = w // 352
    return im.crop((16 * s, 24 * s, 336 * s, 280 * s)).resize((320, 256), Image.NEAREST)


frames = []
with Emulator(machine="next") as e:
    e.load(HERE.parent / "build" / "planet_chomp.nex")
    e.run_until("pc: ready", 600)
    for _ in range(25):
        e.run(EVERY)
        frames.append(grab(e))
    e.run_until("pc: demo starts", 3000)
    for _ in range(110):
        e.run(EVERY)
        frames.append(grab(e))
    log = ""
    while "invincible for 450" not in log or log.count("invincible") < 3:   # the third key (9210 steps in)
        e.run(50)
        log += e.log()
    log = ""
    for n in range(140):
        e.run(EVERY)
        frames.append(grab(e))
        log += e.log()
        if "pc: eat " in log and n > 60:
            for _ in range(15):
                e.run(EVERY)
                frames.append(grab(e))
            break
out = ROOT / "docs" / "images" / "planet_chomp.gif"
frames[0].save(out, save_all=True, append_images=frames[1:], duration=EVERY * 20, loop=0, optimize=True)
print(f"{out}: {len(frames)} frames, {out.stat().st_size // 1024} KB")
