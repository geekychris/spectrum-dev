"""fps with parts of the frame switched off (render_skip bits)"""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))
from zxdev.emu import Emulator
NEX = ROOT / "projects/scairy_fun/build/scairy_fun.nex"
for skip, name in [(0, "all"), (1, "-scenery"), (2, "-sides"), (32, "-side bands"), (64, "-slope quads"),
                   (4, "-bands"), (8, "-sky/ground"), (16, "-skyline,rails.."), (128, "-sim"), (255, "nothing")]:
    with Emulator(machine="next") as e:
        e.load(NEX)
        e.run_until("scairy: ready", 300)
        e.write_mem("render_skip", bytes([skip]))
        e.run_until("scairy: fps", 3000)
        e.run_until("scairy: fps", 3000)
        m = re.findall(r"fps\*10=(\d+)", e.log(0))
        print(f"{name:12s} fps*10 {m[-1] if m else '?'}")
