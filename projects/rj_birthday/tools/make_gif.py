"""docs/images/rj_birthday.gif: the title, then the test's bot at the party:
through the foyer into the Amsterdam lounge (the house scrolling), plants
picked up, the pot police, busted.

    .venv/bin/python projects/rj_birthday/tools/make_gif.py   (from the repo root)
"""
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
PROJ = HERE.parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.path.insert(0, str(HERE))
from zxdev.emu import Emulator  # noqa: E402
from bot import ROOM_AMSTERDAM, ST_JAIL, ST_PLAYING, gs_addr, read_gs, walk  # noqa: E402

EVERY = 4


def grab(e):
    im = e.screenshot(border=True).convert("RGB")
    s = im.size[0] // 352                # 352 x 304 at 2x: the 320 x 256 picture at (16, 24)
    return im.crop((16 * s, 24 * s, 336 * s, 280 * s)).resize((320, 256), Image.NEAREST)


frames = []
GS = gs_addr(PROJ / "build" / "rj_birthday.map")
with Emulator(machine="next") as e:
    e.load(PROJ / "build" / "rj_birthday.nex")
    e.run_until("rj: state title", 400)
    for _ in range(16):
        e.run(EVERY)
        frames.append(grab(e))
    e.press("FIRE")
    plants = 0
    for _ in range(400):
        g = read_gs(e, GS)
        if g["state"] == ST_JAIL:
            for _ in range(12):
                e.run(EVERY)
                frames.append(grab(e))
            break
        if g["state"] != ST_PLAYING:
            break
        if g["room"] != ROOM_AMSTERDAM or g["x"] < 340:
            walk(e, g, 480, 200, EVERY)
        elif g["cops"] and plants >= 2:
            cx, cy = min(g["cops"], key=lambda c: abs(c[0] - g["x"]) + abs(c[1] - g["y"]))
            walk(e, g, cx, cy, EVERY)
        elif g["plants"]:
            px, py = min(g["plants"], key=lambda p: abs(p[0] - g["x"]) + abs(p[1] - g["y"]))
            n = len(g["plants"])
            walk(e, g, px, py, EVERY)
            plants += len(read_gs(e, GS)["plants"]) < n
        else:
            walk(e, g, 480, 200, EVERY)
        frames.append(grab(e))
both = Image.new("RGB", (640, 256))          # one palette for the title's photo and the house
both.paste(frames[0], (0, 0))
both.paste(frames[len(frames) * 2 // 3], (320, 0))
pal = both.quantize(colors=160, method=Image.Quantize.MEDIANCUT)
out = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
dst = ROOT / "docs" / "images" / "rj_birthday.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=EVERY * 20, loop=0, optimize=True)
print(f"{dst}: {len(out)} frames, {dst.stat().st_size} bytes")
