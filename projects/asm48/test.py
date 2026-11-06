"""asm48 test: sjasmplus snapshot loads, logs via ZXLOG, reads SPACE."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

SNA = Path(__file__).resolve().parent / "build" / "asm48.sna"
failures = []


def check(name, ok):
    print(f"  {'PASS' if ok else 'FAIL'} {name}")
    if not ok:
        failures.append(name)


with Emulator(machine="48k") as emu:
    emu.load(SNA)
    check("snapshot runs and logs ready", emu.run_until("asm48: ready", 50))
    check("sjasmplus symbols loaded (.sym)", emu.symbols.lookup("main_loop") is not None)
    emu.press("SPACE")
    check("SPACE detected", emu.run_until("asm48: space", 20))
    before = emu.peek("frames")
    emu.run(10)
    check("main loop counts frames", (emu.peek("frames") - before) & 0xFF == 10)

print(f"{'FAIL' if failures else 'PASS'}: asm48")
sys.exit(1 if failures else 0)
