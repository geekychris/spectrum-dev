"""docs/images/scairy_fun.gif: the title, then the Levee Road up to the old
lane lines (the autopilot's path heading for the embankment)."""
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator  # noqa: E402
from PIL import Image  # noqa: E402

out = ROOT / "docs" / "images" / "scairy_fun.gif"
tmp = ROOT / "build" / "scairy_gif"
with Emulator(machine="next") as e:
    e.load(ROOT / "projects/scairy_fun/build/scairy_fun.nex")
    e.run_until("scairy: title", 3000)
    e.run(100)
    e.record_video(tmp / "a", frames=150, every=6)
    e.press("SPACE", 4, 12)
    e.run(30)
    e.press("SPACE", 4, 12)
    e.run(5600)                                   # towards the old lane lines
    e.record_video(tmp / "b", frames=900, every=6)
frames = [Image.open(p).convert("RGB") for d in ("a", "b") for p in sorted((tmp / d).glob("*.png"))]
frames = [f.crop((32, 32, f.width - 32, f.height - 48)).quantize(64) for f in frames]
frames[0].save(out, save_all=True, append_images=frames[1:], duration=120, loop=0, optimize=True)
print(out, len(frames), "frames", out.stat().st_size, "bytes")
