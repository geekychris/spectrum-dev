"""docs/images/rotoball.gif: the title, then the test's bot against the CPU:
the CPU scores, the bot tackles it and carries the ball into its goal.

    .venv/bin/python projects/rotoball/tools/make_gif.py   (from the repo root)
"""
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE))
from zxdev.emu import Emulator  # noqa: E402
from bot import C_BACK, C_FWD, C_LEFT, C_RIGHT, PROBE_SIZE, Bot, decode  # noqa: E402

KEYS = [(C_FWD, "W"), (C_BACK, "S"), (C_LEFT, "A"), (C_RIGHT, "D")]
EVERY = 4


def grab(e):
    im = e.screenshot(border=True).convert("RGB")
    w, h = im.size                       # 352 x 304 at 2x: the 320 x 256 picture at (16, 24)
    s = w // 352
    return im.crop((16 * s, 24 * s, 336 * s, 280 * s)).resize((320, 256), Image.NEAREST)


frames = []
with Emulator(machine="next") as e:
    e.load(HERE.parent / "build" / "rotoball.nex")
    e.run_until("roto: ready", 300)
    for _ in range(40):
        e.run(EVERY)
        frames.append(grab(e))
    e.press("SPACE")
    bot, down, t, end = Bot(), "", 0, None
    while t < 1500:
        p = decode(e.read_mem("roto_probe", PROBE_SIZE))
        keys = " ".join(k for bit, k in KEYS if bot.step(p) & bit)
        if keys != down:
            e.release()
            if keys:
                e.hold(keys)
            down = keys
        e.run(1)
        t += 1
        # the CPU's run up to its goal goes by at double speed
        if t % (EVERY * (2 if bot.phase == "concede" and t < 230 else 1)) == 0:
            frames.append(grab(e))
        if end is None and "roto: goal P1" in e.log(0):
            end = t + 45
        if end is not None and t >= end:
            break
sheet = Image.new("RGB", (320, 256 * 8))         # one palette from frames all through it
for k in range(8):
    sheet.paste(frames[k * (len(frames) - 1) // 7], (0, 256 * k))
pal = sheet.quantize(colors=32, method=Image.Quantize.MEDIANCUT)
out = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
dst = ROOT / "docs" / "images" / "rotoball.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=EVERY * 20, loop=0, optimize=True)
print(f"{dst}: {len(out)} frames, {dst.stat().st_size} bytes")
