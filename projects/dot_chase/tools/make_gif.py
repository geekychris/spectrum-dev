"""docs/images/dot_chase.gif: the test's bot plays; every second frame of a
stretch with a flower eaten and the spiders scared.

    .venv/bin/python projects/dot_chase/tools/make_gif.py
"""
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
sys.argv = sys.argv[:1]
_src = (HERE.parent / "test.py").read_text()
bot = {"__file__": str(HERE.parent / "test.py")}
exec(compile(_src[:_src.index("SHOTS.mkdir")], "test.py", "exec"), bot)

OUT = ROOT / "docs" / "images" / "dot_chase.gif"
START, FRAMES = 1150, 360                     # frames of play before, frames shown


def main():
    e = bot["Emulator"]()
    try:
        e.load(bot["TAP"])
        e.run_until("dot: ready", 3000)
        e.run(30)
        e.press("SPACE")
        e.run_until("state=playing", 300)
        held, shots = None, []
        for t in range(START + FRAMES):
            maze, p = bot["read"](e)
            if p[3] == 2:
                k = bot["KEYS"][bot["choose"](maze, p, False)]
                if k != held:
                    e.release()
                    e.hold(k)
                    held = k
            e.run(1)
            if t >= START and t % 2 == 0:
                shots.append(e.screenshot(border=False).convert("P", palette=Image.ADAPTIVE, colors=16))
        shots[0].save(OUT, save_all=True, append_images=shots[1:], duration=40, loop=0, optimize=True)
        print(f"{OUT} ({len(shots)} frames, {OUT.stat().st_size // 1024} KB)")
    finally:
        e.close()


if __name__ == "__main__":
    main()
