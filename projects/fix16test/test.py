"""SDK module fix16 (and fixmath's isqrt32) (./zx test fix16test), against a
Python model: 2000 pseudo-random pairs (small, medium, full-range and edge
values) through fmul, fdiv, a3_angle, fhypot, fsqrt and fsin, hashed; the
model is the amiga3d engine's 64-bit definitions. isqrt32 on values up to
2^32 - 1."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "zxdev" / "src"))

from zxdev.emu import Emulator  # noqa: E402

HERE = Path(__file__).resolve().parent
NEX = HERE / "build" / "fix16test.nex"
failures = []


def check(name, ok, detail=""):
    print(f"  {'PASS' if ok else 'FAIL'} {name}{f'  ({detail})' if detail and not ok else ''}")
    if not ok:
        failures.append(name)


# ---- fix16's model ----
src = (ROOT / "sdk" / "src" / "fix16_tab.c").read_text()
SIN = [int(v) for v in re.findall(r"-?\d+", src[src.index("{") + 1:src.index("};")])]


def s32(v):
    v &= 0xFFFFFFFF
    return v - (1 << 32) if v & 0x80000000 else v


def tdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def isqrt(v):
    r, bit = 0, 1 << 30
    while bit > v:
        bit >>= 2
    while bit:
        if v >= r + bit:
            v -= r + bit
            r = (r >> 1) + bit
        else:
            r >>= 1
        bit >>= 2
    return r


def fhypot(a, b):
    x, y, sh = abs(a), abs(b), 0
    while (x | y) >= 46000:
        x >>= 1
        y >>= 1
        sh += 1
    return s32(isqrt(x * x + y * y) << sh)


def fsqrt(a):
    if a <= 0:
        return 0
    v, k = a, 0
    while v < (1 << 28) and k < 15:
        v <<= 2
        k += 1
    r = isqrt(v)
    return s32(r << (8 - k) if k <= 8 else r >> (k - 8))


def angle(rad):
    v = ((rad * 683565276) >> 32) & 0xFFFF
    return v - 65536 if v & 0x8000 else v


def fsin(rad):
    a = angle(rad) & 0xFFFF
    i, f = a >> 8, a & 255
    s0, s1 = SIN[i], SIN[(i + 1) & 255]
    return s0 + (((s1 - s0) * f) >> 8)


def fix16_hash():
    s = [12345]

    def rnd():
        x = s[0]
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        s[0] = x
        k = x & 3
        if k == 0:
            return (x >> 8) - 8388608
        if k == 1:
            return (x >> 15) - 65536
        if k == 2:
            return s32(x)
        return s32(0x80000000 + ((x >> 3) & 3)) if x & 4 else 0x7FFFFFFF - ((x >> 3) & 3)

    h = [2166136261]

    def mix(v):
        h[0] = ((h[0] ^ (v & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF

    for _ in range(2000):
        a, b = rnd(), rnd()
        mix(s32((a * b) >> 16))
        if b:
            mix(s32(tdiv(a * 65536, b)))
        mix(angle(a))
        mix(fhypot(a >> 8, b >> 8))
        mix(fsqrt(a & 0x7FFFFFFF))
        mix(fsin(a >> 12))
    return h[0]


with Emulator(machine="next") as e:
    e.load(NEX)
    check("runs", e.run_until("ff: done", 2000))
    log = e.log(0)
    import math
    got = [(int(v), int(r)) for v, r in re.findall(r"ff: isqrt (\d+) (\d+)", log)]
    check("isqrt32 (fixmath.h): floor(sqrt) up to 2^32 - 1", len(got) == 10 and all(r == math.isqrt(v) for v, r in got),
          str([g for g in got if g[1] != math.isqrt(g[0])]))
    m = re.search(r"ff: fix16 ([0-9A-F]{8})", log)
    want = f"{fix16_hash():08X}"
    check(f"fix16: fmul, fdiv, a3_angle, fhypot, fsqrt, fsin over 2000 pairs = the 64-bit model ({want})",
          m is not None and m.group(1) == want, m.group(1) if m else "missing")

print(f"{'FAIL' if failures else 'PASS'}: fix16test")
sys.exit(1 if failures else 0)
