"""selftest: sdk/template builds and passes its own test.py for 48K and Next.

Instantiates the template (like ./zx new) under build/template-test/, builds
it with bin/zx-make and runs the generated test.py.
"""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
fails = 0
for target in ("zx", "zxn"):
    name = f"tmpl_{target}"
    dest = ROOT / "build" / "template-test" / name
    shutil.rmtree(dest, ignore_errors=True)
    shutil.copytree(ROOT / "sdk" / "template", dest)
    for f in ("Makefile", "src/main.c", "test.py"):
        p = dest / f
        p.write_text(p.read_text().replace("__NAME__", name).replace("__TARGET__", target))
    r = subprocess.run([str(ROOT / "bin" / "zx-make"), "-C", str(dest)], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
        print(f"  FAIL {name}: build")
        fails += 1
        continue
    r = subprocess.run([sys.executable, str(dest / "test.py")], capture_output=True, text=True)
    print(r.stdout + r.stderr)
    fails += r.returncode != 0
print("FAIL" if fails else "PASS")
sys.exit(1 if fails else 0)
