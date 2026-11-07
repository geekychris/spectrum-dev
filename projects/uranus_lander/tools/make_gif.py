"""docs/images/uranus_lander.gif: the title, then the autopilot landing on
level 1 and the start of level 2. A picture is taken each time the game
finishes a screen (at frame_wait), every `--every` screens, so no frame is
caught half drawn. Run: .venv/bin/python projects/uranus_lander/tools/make_gif.py"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("-o", default=str(ROOT / "docs" / "images" / "uranus_lander.gif"))
ap.add_argument("--every", type=int, default=2)
args = ap.parse_args()

TAP = ROOT / "projects" / "uranus_lander" / "build" / "uranus_lander.tap"
pics, waits = [], []


def screen(e):
    """run to the next finished screen; return the frame it was finished in"""
    e.break_at("_frame_wait")
    e.continue_(20)
    e.delete()
    return e.position()["since_load"]


def grab(e, n):
    last = None
    for i in range(n):
        f = screen(e)
        if i % args.every == 0:
            if last is not None:
                waits.append(max(20, (f - last) * 20))     # ms: as long as it was on screen
            pics.append(e.screenshot().convert("RGB"))
            last = f
    waits.append(40)


with Emulator() as e:
    e.load(TAP)
    e.run_until("uranus: ready", 3000)
    grab(e, 60)
    e.press("SPACE")
    e.run_until("uranus: level=1", 200)
    e.press("Z")
    for _ in range(4000):
        grab(e, 2)
        if "landed level=1" in e.log_text(0):
            break
    grab(e, 70)
pics[0].save(args.o, save_all=True, append_images=pics[1:], duration=waits[:len(pics)], loop=0, optimize=True)
print(f"{args.o}: {len(pics)} pictures, {Path(args.o).stat().st_size // 1024} KB")
