"""docs/images/boing_twist.gif: the title (the Boing ball bouncing round its
room), choosing the Coral Reef (it brings the Aquarium room), the scramble,
then the scramble undone by hand: twists, flips (one seen from above), and
the solved bounce.

    .venv/bin/python projects/boing_twist/tools/make_gif.py   (from the repo root)
"""
import struct
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

EVERY = 6
OFF_HIST, OFF_NHIST = 4 + 256 + 1024, 4 + 256 + 1024 + 2048


def grab(e):
    im = e.screenshot(border=True).convert("RGB")
    w, h = im.size                       # 352 x 304 at 2x: the 320 x 256 picture at (16, 24)
    s = w // 352
    return im.crop((16 * s, 24 * s, 336 * s, 280 * s)).resize((320, 256), Image.NEAREST)


def film(e, frames, n):
    for _ in range(n):
        e.run(EVERY)
        frames.append(grab(e))


def tap(e, frames, key):
    e.press(key, hold_frames=2, release_frames=2)
    frames.append(grab(e))


frames = []
with Emulator(machine="next") as e:
    e.load(HERE.parent / "build" / "boing_twist.nex")
    e.run_until("bt: screen TITLE", 300)
    e.run(50)
    film(e, frames, 25)
    e.press("SPACE")
    film(e, frames, 6)
    tap(e, frames, "UP")                         # Medium
    tap(e, frames, "RIGHT")
    tap(e, frames, "DOWN")
    for _ in range(7):                           # down the list to the Coral Reef
        tap(e, frames, "RIGHT")
        film(e, frames, 1)
    film(e, frames, 15)
    e.press("ENTER")
    while "bt: scrambled" not in e.log(0):
        film(e, frames, 1)
    film(e, frames, 6)
    raw = e.read_mem("_P", OFF_NHIST + 2)
    segs = struct.unpack_from("<h", raw, 2)[0]
    hist = list(struct.unpack_from(f"<{struct.unpack_from('<h', raw, OFF_NHIST)[0]}h", raw, OFF_HIST))
    for i, m in enumerate(reversed(hist)):
        m = m if m & 0x100 else m ^ 1
        if i == 3:
            tap(e, frames, "W")
            tap(e, frames, "W")
        if m & 0x100:
            while e.read_mem("_sel_half", 1)[0] != (m & 0xFF):
                d = ((m & 0xFF) - e.read_mem("_sel_half", 1)[0]) % segs
                tap(e, frames, "X" if d <= segs // 2 else "Z")
                film(e, frames, 2)
            tap(e, frames, "SPACE")
        else:
            while e.read_mem("_sel_band", 1)[0] != (m >> 1) & 0x7F:
                tap(e, frames, "DOWN" if e.read_mem("_sel_band", 1)[0] < (m >> 1) & 0x7F else "UP")
            tap(e, frames, "LEFT" if m & 1 else "RIGHT")
        film(e, frames, 4)
        if "bt: solved" in e.log(0):
            break
    film(e, frames, 30)
sheet = Image.new("RGB", (320, 256 * 8))         # one palette from frames all through it
for k in range(8):
    sheet.paste(frames[k * (len(frames) - 1) // 7], (0, 256 * k))
pal = sheet.quantize(colors=64, method=Image.Quantize.MEDIANCUT)
out = [f.quantize(palette=pal, dither=Image.Dither.NONE) for f in frames]
dst = ROOT / "docs" / "images" / "boing_twist.gif"
out[0].save(dst, save_all=True, append_images=out[1:], duration=EVERY * 20, loop=0, optimize=True)
print(f"{dst}: {len(out)} frames, {dst.stat().st_size} bytes")
