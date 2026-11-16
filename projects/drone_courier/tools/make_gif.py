"""docs/images/drone_courier.gif: the title with the autopilot flying behind
it, then a game on the autopilot (B): off the depot, over the city to the
customer's pad and down onto it. A picture every `--every` frames, each a
whole frame (the emulator finishes the frame before it is taken).
Run: .venv/bin/python projects/drone_courier/tools/make_gif.py"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parents[1]
ap = argparse.ArgumentParser()
ap.add_argument("-o", default=str(ROOT / "docs" / "images" / "drone_courier.gif"))
ap.add_argument("--every", type=int, default=14)
ap.add_argument("--frames", type=int, default=1100)
args = ap.parse_args()

pics = []
with Emulator(machine="next") as e:
    e.load(HERE / "build" / "drone_courier.nex")
    e.run_until("dc: ready", 600)
    for i in range(15):
        e.run(args.every)
        pics.append(e.screenshot().convert("RGB"))
    e.press("SPACE")
    e.run(20)
    e.press("B")
    log = ""
    for t in range(0, args.frames, args.every):
        e.run(args.every)
        pics.append(e.screenshot().convert("RGB"))
        log += e.log()
        if "dc: ev DELIVER" in log:
            for k in range(6):
                e.run(args.every)
                pics.append(e.screenshot().convert("RGB"))
            break
from PIL import Image  # noqa: E402
pics = [p.resize((352, 304)) for p in pics]
mosaic = Image.new("RGB", (352 * 4, 304))
for i, k in enumerate((2, len(pics) // 3, 2 * len(pics) // 3, len(pics) - 1)):
    mosaic.paste(pics[k], (i * 352, 0))
pal = mosaic.quantize(min(256, len(set(mosaic.getdata()))), method=Image.Quantize.FASTOCTREE)
pics = [p.quantize(palette=pal, dither=Image.Dither.NONE) for p in pics]
pics[0].save(args.o, save_all=True, append_images=pics[1:], duration=20 * args.every, loop=0, optimize=True)
print(f"{args.o}: {len(pics)} pictures, {Path(args.o).stat().st_size // 1024} KB")
