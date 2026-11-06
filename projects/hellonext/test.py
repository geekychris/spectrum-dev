"""hellonext tests (run by `./zx test hellonext`, part of `./zx selftest`).

Direct .nex loading on the Next, Next registers (machine id, 28 MHz),
Layer 2 output, keyboard input, frame pacing.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
NEX = HERE / "build" / "hellonext.nex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail else ''}")
    if not ok:
        failures.append(name)


with Emulator(machine="next") as emu:
    emu.load(NEX)
    check("runs on the Next", emu.status()["machine"] == "next")
    check("logs ready", emu.run_until("hellonext: ready", 2000), f"frame {emu.last_run['frame']}")
    log = emu.log(0)
    check("Next machine id read", "machine=" in log and "machine=0 " not in log, log.splitlines()[0])
    check("CPU at 28 MHz", "speed=3" in log)

    emu.run(10)
    emu.press("X")
    check("key X is logged", emu.run_until("hellonext: key x", 20))
    check("frame 250 tick", emu.run_until("hellonext: frame 250", 400))
    f250 = emu.last_run["frame"]
    emu.run_until("hellonext: frame 500", 400)
    check("50 Hz main loop", emu.last_run["frame"] - f250 == 250, f"{emu.last_run['frame'] - f250} frames")

    img = emu.screenshot(HERE / "build" / "test.png")
    check("screenshot is 704x608", img.size == (704, 608), str(img.size))
    # Layer 2 gradient fills the lower paper area; the ULA text shows above it
    l2 = img.crop((96, 112 + 2 * 60, 96 + 512, 112 + 2 * 190))
    check("Layer 2 gradient visible", len(l2.getcolors(1 << 16) or []) >= 32, f"{len(l2.getcolors(1 << 16) or [])} colours")
    text = img.crop((96, 112, 96 + 512, 112 + 2 * 40))
    check("ULA text visible through Layer 2", len(text.getcolors(1 << 16) or []) >= 2)
    if failures:
        print("\n".join(emu.debug_log[-15:]))

print(f"{'FAIL' if failures else 'PASS'}: hellonext")
sys.exit(1 if failures else 0)
