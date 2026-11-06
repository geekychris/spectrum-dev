"""inputtest smoke test: boots and reports keyboard + Kempston changes.
(The full input matrix is checked by tools/zxdev/tests/test_features.py.)"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

TAP = Path(__file__).resolve().parent / "build" / "inputtest.tap"
failures = []


def check(name, ok):
    print(f"  {'PASS' if ok else 'FAIL'} {name}")
    if not ok:
        failures.append(name)


with Emulator() as emu:
    emu.load(TAP)
    check("boots", emu.run_until("inputtest: ready", 3000))
    emu.run(5)
    emu.press("ENTER")
    check("ENTER -> row BF bit 0", emu.run_until("inputtest: row bf=1e", 10))
    emu.press("FIRE")
    check("Kempston fire", emu.run_until("inputtest: kempston=10", 10))

print(f"{'FAIL' if failures else 'PASS'}: inputtest")
sys.exit(1 if failures else 0)
