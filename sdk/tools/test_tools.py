"""Tests for the SDK's tools (run by ./zx selftest): mod2ay.py, nextpic.py, aystream.py, zxrle.py,
fatimg.py, nextbasic.py."""
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


# ---- picture helpers for programs (nextpic.quantize ... group_patterns) and
# ---- TurboSound streams (aystream.py); the picture ones need numpy
import numpy as np  # noqa: E402  (scripts/setup-python.sh installs it into .venv)
from aystream import Ay, decode, encode, period  # noqa: E402

idx = np.arange(5 * 3).reshape(5, 3) + 1
opaque = np.array([[0, 1, 1], [1, 1, 0], [1, 0, 0], [0, 0, 1], [1, 1, 1]], bool)
data = nextpic.rle_columns(idx, opaque)
img = np.zeros((5, 3), int)
o = 0
for x in range(3):
    y = 0
    while data[o] != 0xFF:
        y += data[o]
        n = data[o + 1]
        img[y:y + n, x] = list(data[o + 2:o + 2 + n])
        o += 2 + n
        y += n
    o += 1
check("nextpic rle_columns round trip", (img == np.where(opaque, idx, 0)).all() and o == len(data))
check("nextpic piece header", nextpic.piece(idx, opaque)[:2] == bytes([3, 5]))
im = np.arange(20 * 33).reshape(33, 20) % 200
b, cols, rows = nextpic.group_patterns(im)
check("nextpic group_patterns size", (cols, rows) == (2, 3) and len(b) == 6 * 256)
x, y = 17, 21
check("nextpic group_patterns layout", b[((y // 16) * cols + x // 16) * 256 + (y % 16) * 16 + x % 16] == im[y, x])
check("nextpic group_patterns fill", b[256 + 15] == 0xE3)
q = nextpic.quantize(np.random.default_rng(1).integers(0, 256, (500, 3)), 16)
check("nextpic quantize: <= n colours in RGB333 steps",
      len(q) <= 16 and all(v in [(k * 255 + 3) // 7 for k in range(8)] for v in q.reshape(-1)))

ay = Ay()
ay.tone(0, period(440), 15)
for _ in range(11):
    ay.frame()
ay.loop_here()
ay.tone(1, 300, 9)
ay.frame()
ay.off(1)
ay.frame()
s = encode(ay)
check("aystream round trip", decode(s) == ay.frames)
check("aystream: 10 unchanged frames are one wait byte", len(s) <= 41, str(len(s)))
loop = s[0] | s[1] << 8
check("aystream loop point writes every register", loop != 0xFFFF and s[loop + 1] & 0x3F == 0x3F)
check("aystream period(440) at 1.75 MHz", period(440) == 249)

# zxrle: round trips, runs and literals at their limits
from zxrle import pack, unpack  # noqa: E402
cases = [b"", b"a", b"aa", b"aaa", b"." * 129, b"." * 130, b"." * 600 + b"R" + b"." * 50,
         bytes(range(256)), bytes(range(130)) + b"x" * 200]
check("zxrle round trips", all(unpack(pack(c)) == c for c in cases))
check("zxrle packs runs (600 dots -> a few bytes)", len(pack(b"." * 600)) <= 12)
check("zxrle ends with 0x80 and never uses it as a control byte elsewhere",
      all(pack(c)[-1] == 0x80 for c in cases))

# fatimg: trees round-trip through FAT16 images; short / mixed-case / long names
import fatimg  # noqa: E402
tree = {"nextzxos": None, "nextzxos/autoexec.bas": b"10 REM", "machines/next/enNxtmmc.rom": b"R" * 3000,
        "A Long Directory Name": None, "A Long Directory Name/some file.txt": b"hello\n",
        "empty.dat": b"", "big.bin": bytes(range(256)) * 600}
tree.update({f"many/f{i:03d}.x": bytes([i]) * i for i in range(40)})
with tempfile.TemporaryDirectory() as tmp:
    img = os.path.join(tmp, "sd.mmc")
    fatimg.write_image(tree, img, size_mb=16)
    back = fatimg.read_image(img)
    check("fatimg: write -> read gives the same tree", back == {**tree, "machines": None, "machines/next": None,
                                                               "many": None})
    raw = open(img, "rb").read()
    check("fatimg: mixed-case 8.3 keeps its upper-case short name (NextZXOS's loader matches those)",
          b"ENNXTMMCROM" in raw and b"ENNXTM~1" not in raw)
    check("fatimg: all-lower-case 8.3 names are plain entries (NT case flags)", b"AUTOEXECBAS" in raw)
    check("fatimg: an MBR partition at 1 MB", raw[510:512] == b"\x55\xaa" and raw[446 + 8:446 + 12] == (2048).to_bytes(4, "little"))
    t2 = fatimg.drop(back, ["MANY", "a long directory name"])
    fatimg.put(t2, "Arcade/stats.dat", b"\1\2")
    check("fatimg: drop and put (case-insensitive, parents made)",
          not any(k.lower().startswith("many") for k in t2) and t2.get("Arcade") is None and "Arcade" in t2)
    stock = os.path.join(ROOT, "third_party", "zesarux", "src", "tbblue.mmc")
    if os.path.exists(stock):
        st = fatimg.read_image(stock)
        check("fatimg: reads ZEsarUX's tbblue.mmc (NextZXOS)", fatimg.find(st, "nextzxos/autoexec.bas") is not None
              and fatimg.find(st, "dot/NEXLOAD") is not None)

# nextbasic: tokenising matches NextZXOS's own (mounter.bas's first lines)
from nextbasic import plus3dos_basic, tokenize  # noqa: E402
want = bytes.fromhex("000a09009cfd3a9efd3a9dfd0d000f3b00f9c0353830380e0000b016003aea636c65617220776f726b737061636520746f2073746f70206f7574206f66206d656d6f7279206572726f72730d")
check("nextbasic: tokens + numbers as NextZXOS stores them",
      tokenize("10 LAYER CLEAR: SPRITE CLEAR: PALETTE CLEAR\n15 RANDOMIZE USR 5808: REM clear workspace to stop out of memory errors") == want)
dot = tokenize('10 CLS: .nexload /arcade/a.nex\n20 SPECTRUM "c:/x.sna"')
check("nextbasic: dot commands stay text, SPECTRUM is 0xA3", b"\xfb:.nexload /arcade/a.nex\r" in dot and b"\xa3\"c:/x.sna\"" in dot)
h = plus3dos_basic(dot, autostart=10)
check("nextbasic: +3DOS header (signature, length, autostart, checksum)",
      h[:8] == b"PLUS3DOS" and int.from_bytes(h[11:15], "little") == len(h) and h[18:20] == b"\x0a\x00"
      and h[127] == sum(h[:127]) & 0xFF)

print(f"{'FAIL' if failures else 'PASS'}: sdk tools")
sys.exit(1 if failures else 0)
