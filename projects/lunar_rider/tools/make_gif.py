"""docs/images/lunar_rider.gif: the title, then the riding bot of test.py
(jumping craters, shooting) through the first checkpoint. A picture every
`--every` frames, each a whole frame (the emulator finishes the frame before
it is taken). Run: .venv/bin/python projects/lunar_rider/tools/make_gif.py"""
import argparse
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument("-o", default=str(ROOT / "docs" / "images" / "lunar_rider.gif"))
ap.add_argument("--every", type=int, default=5)
ap.add_argument("--frames", type=int, default=420)
args = ap.parse_args()

# the bot's rule from test.py (without running the test)
src = (HERE / "test.py").read_text()
ns = {}
exec(src[src.index("CRATER_SM"):src.index("def bot_game")], ns)
exec("import struct\n" + src[src.index("def probe"):src.index("def danger")], ns)
danger, probe = ns["danger"], ns["probe"]

pics = []
with Emulator(machine="next") as e:
    e.load(HERE / "build" / "lunar_rider.nex")
    e.run_until("lunar: ready", 600)
    for i in range(20):
        e.run(args.every)
        pics.append(e.screenshot().convert("RGB"))
    e.press("SPACE")
    held = None
    for t in range(args.frames):
        p = probe(e)
        if p["state"] == 3:
            break
        keys = "A+Q" if p["state"] in (1, 4) and p["on_ground"] and danger(p) else "A"
        if keys != held:
            e.release()
            e.hold(keys)
            held = keys
        e.run(1)
        if t % args.every == 0:
            pics.append(e.screenshot().convert("RGB").resize((352, 304)))
from PIL import Image  # noqa: E402
pics = [p.resize((352, 304)) for p in pics]
mosaic = Image.new("RGB", (352 * 4, 304))
for i, k in enumerate((2, len(pics) // 3, 2 * len(pics) // 3, len(pics) - 1)):
    mosaic.paste(pics[k], (i * 352, 0))
# every colour the game showed (well under 256: the Next's palette is exact)
pal = mosaic.quantize(min(256, len(set(mosaic.getdata()))), method=Image.Quantize.FASTOCTREE)
pics = [p.quantize(palette=pal, dither=Image.Dither.NONE) for p in pics]
pics[0].save(args.o, save_all=True, append_images=pics[1:], duration=20 * args.every, loop=0, optimize=True)
print(f"{args.o}: {len(pics)} pictures, {Path(args.o).stat().st_size // 1024} KB")
