"""Tests for the SDK's tools (run by ./zx selftest): mod2ay.py and nextpic.py."""
import os
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import nextpic  # noqa: E402

failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


# mod2ay: StakAttack's song, converted again, is the committed one
stak = os.path.join(ROOT, "projects", "stakattack")
out = subprocess.run(["sh", os.path.join(stak, "tools", "make_music.sh")], capture_output=True, text=True)
diff = subprocess.run(["git", "-C", ROOT, "diff", "--quiet", "--", "projects/stakattack/src/music.c"])
check("mod2ay: StakAttack's MOD converts to the committed music.c", out.returncode == 0 and diff.returncode == 0, out.stderr[-300:])

with tempfile.TemporaryDirectory() as tmp:
    # nextpic l2: a 320 x 256 picture, column-major, banks, palette
    img = Image.new("RGB", (320, 256), (0, 0, 0))
    img.putpixel((1, 2), (255, 0, 0))
    img.putpixel((300, 250), (0, 255, 0))
    src = os.path.join(tmp, "pic.png")
    img.save(src)
    base = os.path.join(tmp, "out", "pic")
    r = subprocess.run([sys.executable, os.path.join(HERE, "nextpic.py"), "l2", src, "--out", base, "--bank", "40"],
                       capture_output=True, text=True)
    banks = [open(f"{base}_{k}.bin", "rb").read() for k in range(5)] if r.returncode == 0 else []
    data = b"".join(banks)
    pal = open(base + "_pal.c").read() if r.returncode == 0 else ""
    asm = open(base + ".asm").read() if r.returncode == 0 else ""
    check("nextpic l2: five 16K banks", len(banks) == 5 and all(len(b) == 16384 for b in banks), r.stderr[-200:])
    if data:
        red, green = data[1 * 256 + 2], data[300 * 256 + 250]
        palv = [int(v) for v in pal.split("{")[1].split("}")[0].replace("\n", "").split(",") if v.strip()]
        check("nextpic l2: column-major (x * 256 + y)", red != 0 and green not in (0, red) and data[2 * 256 + 1] == data[0])
        check("nextpic l2: palette in RGB333", palv[red] == 0x1C0 and palv[green] == 0x038, f"{palv[red]:03x} {palv[green]:03x}")
        check("nextpic l2: BANK sections for the .nex", "SECTION BANK_40" in asm and "SECTION BANK_44" in asm)
    check("nextpic: colours never the transparent 0xE3", nextpic.avoid_transparent([0xE3 << 1, 0xE3 << 1 | 1, 5]) == [0x1C4, 0x1C5, 5])

    # nextpic sprites: transparency from alpha, quads
    sheet = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    for y in range(16):
        sheet.putpixel((20, y), (255, 255, 255, 255))      # top right quadrant
    pats = nextpic.patterns(sheet, quad=True)
    check("nextpic sprites: a 32 x 32 cell is four patterns", len(pats) == 4)
    check("nextpic sprites: alpha 0 is index 0xE3, the pixel in the top right",
          pats[0][0] == 0xE3 and pats[1][4] != 0xE3 and all(v == 0xE3 for v in pats[2]))
    check("nextpic: quad_patterns matches patterns' order",
          nextpic.quad_patterns(list(range(1024))) [1][0] == 16 and nextpic.quad_patterns(list(range(1024)))[2][0] == 512)

print(f"{'FAIL' if failures else 'PASS'}: sdk tools")
sys.exit(1 if failures else 0)
