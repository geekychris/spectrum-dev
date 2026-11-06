"""hello48 tests (run by `./zx test hello48`, part of `./zx selftest`).

Boot from tape, keyboard input, 50 Hz frame pacing, screen output, and a
determinism check (two emulators, identical results).
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
TAP = HERE / "build" / "hello48.tap"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail else ''}")
    if not ok:
        failures.append(name)


def boot(emu):
    emu.load(TAP)
    return emu.run_until("hello48: ready", 2000)


with Emulator(machine="48k") as emu:
    check("loads from tape and logs ready", boot(emu), f"frame {emu.last_run['frame']}")
    ready_frame = emu.last_run["frame"] - emu.load_frame
    emu.run(10)
    emu.press("SPACE")
    check("SPACE is logged", emu.run_until("hello48: key", 20))
    emu.press("Z")
    check("Z is logged as z", emu.run_until("hello48: key z", 20))

    # interrupts on, HALT-paced: exactly 250 emulated frames between ticks
    check("frame 250 tick", emu.run_until("hello48: frame 250", 400))
    f250 = emu.last_run["frame"]
    emu.run_until("hello48: frame 500", 400)
    check("50 Hz main loop", emu.last_run["frame"] - f250 == 250, f"{emu.last_run['frame'] - f250} frames")

    img = emu.screenshot(HERE / "build" / "test.png")
    check("screenshot is 352x304", img.size == (352, 304), str(img.size))
    r, g, b = img.getpixel((4, 4))
    check("blue border", b > 150 and r < 60 and g < 60, str((r, g, b)))
    check("colour bars drawn", len(img.getcolors(1 << 16) or []) >= 9, f"{len(img.getcolors(1 << 16))} colours")
    paper = img.crop((48, 56, 48 + 256, 56 + 32))
    check("text on the paper", len(paper.getcolors(1 << 16) or []) >= 2)

    check("symbols loaded from .map", emu.symbols.lookup("main") is not None)
    check("read a C global by name", 0 < emu.peek16("frames") < 1000, str(emu.peek16("frames")))
    if failures:
        print("\n".join(emu.debug_log[-15:]))

# same program, same inputs -> same frame numbers and registers
with Emulator(machine="48k") as emu:
    boot(emu)
    again = emu.last_run["frame"] - emu.load_frame
    check("deterministic boot", again == ready_frame, f"ready {again} frames after load, first run {ready_frame}")

print(f"{'FAIL' if failures else 'PASS'}: hello48")
sys.exit(1 if failures else 0)
