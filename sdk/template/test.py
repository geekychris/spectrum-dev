"""Smoke test for __NAME__ (run by `./zx test __NAME__`).

Boots headless, waits for the ready line, moves and fires with keys and the
joystick, checks the log and that something was drawn. Extend it as the
program grows.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
PROG = next((HERE / "build").glob("__NAME__.*[px]"))   # .tap or .nex
failures = []


def check(name, ok):
    print(f"  {'PASS' if ok else 'FAIL'} {name}")
    if not ok:
        failures.append(name)


with Emulator(machine="next" if PROG.suffix == ".nex" else "48k") as emu:
    emu.load(PROG)
    check("boots to ready", emu.run_until("__NAME__: ready", 2000))
    emu.run(5)
    emu.press("P", hold_frames=2)
    check("P moves right", emu.run_until("__NAME__: move x=16 y=12", 20))
    emu.press("RIGHT", hold_frames=2)                  # Kempston joystick
    check("joystick moves right", emu.run_until("__NAME__: move x=17", 20))
    emu.press("SPACE")
    check("SPACE fires", emu.run_until("__NAME__: fire", 20))
    img = emu.screenshot(HERE / "build" / "test.png")
    check("screen has content", len(img.getcolors(1 << 16) or []) >= 3)
    if failures:
        print("\n".join(emu.debug_log[-15:]))

print(f"{'FAIL' if failures else 'PASS'}: __NAME__")
sys.exit(1 if failures else 0)
