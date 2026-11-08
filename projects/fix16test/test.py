"""SDK module fix16 (and fixmath's isqrt32) (./zx test fix16test), against a
Python model: 2000 pseudo-random pairs (small, medium, full-range and edge
values) through fmul, fdiv, a3_angle, fhypot, fsqrt and fsin, hashed; the
model is the amiga3d engine's 64-bit definitions. Then 1000 pairs through
fatan2, a3_atan2, fasin, fhypot3 and fexp_neg (the engine's a3d_fix.c and
a3d_math.c); a3_sin and a3_cos at all 65536 angles. isqrt32 on values up
to 2^32 - 1."""
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


src_atan = (ROOT / "sdk" / "src" / "fix16_atan.c").read_text()
ATAN = [int(v) for v in re.findall(r"\d+", src_atan[src_atan.index("atan_tab[129] = {") + 17:src_atan.index("};")])]


def atan_ratio(num, den):
    a = abs(num)
    if den <= 0:
        return 0
    a = min(a, den)
    while den > 0x7FFFF:
        a >>= 1
        den >>= 1
    t = (a * 2048) // den
    f, i = t & 15, t >> 4
    r = ATAN[i] + ((((ATAN[i + 1] - ATAN[i]) * f) >> 4) if i < 128 else 0)
    return -r if num < 0 else r


def a3_atan2(x, y):
    ax, ay = abs(x), abs(y)
    if not ax and not ay:
        return 0
    a = atan_ratio(ax, ay) if ax <= ay else 16384 - atan_ratio(ay, ax)
    if y < 0:
        a = 32768 - a
    if x < 0:
        a = -a
    return a & 0xFFFF


def fatan2(y, x):
    ax, ay = abs(x), abs(y)
    if not ax and not ay:
        return 0
    t = atan_ratio(ay, ax) if ay <= ax else 16384 - atan_ratio(ax, ay)
    if x < 0:
        t = 32768 - t
    if y < 0:
        t = -t
    return s32((t * 411775) >> 16)


def fasin(x):
    return fatan2(x, fsqrt(65536 - s32((x * x) >> 16)))


def fhypot3(a, b, c):
    x, y, z, sh = abs(a) & 0xFFFFFFFF, abs(b) & 0xFFFFFFFF, abs(c) & 0xFFFFFFFF, 0
    while (x | y | z) >= 37000:
        x, y, z, sh = x >> 1, y >> 1, z >> 1, sh + 1
    return s32(isqrt(x * x + y * y + z * z) << sh)


def fexp_neg(x):
    if x <= 0:
        return 65536
    if x > 20 * 65536:
        return 0
    k = x // 45426
    y = x - k * 45426
    yy = (y * y) >> 16
    r = 65536 - y + yy // 2 - ((yy * y) >> 16) // 6
    return r >> k


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
    hx = h[0]
    h[0] = 2166136261
    for _ in range(1000):
        a, b = rnd(), rnd()
        mix(fatan2(a >> 8, b >> 8))
        mix(a3_atan2(a >> 8, b >> 8))
        mix(fasin(max(-65536, min(65536, a >> 15))))
        mix(fhypot3(a >> 8, b >> 8, s32(a ^ b) >> 8))
        mix(fexp_neg((abs(a) if a != -2147483648 else a) >> 10))     # (C's -a of -2^31 stays negative)
    return hx, h[0]


with Emulator(machine="next") as e:
    e.load(NEX)
    check("runs", e.run_until("ff: done", 2000))
    log = e.log(0)
    import math
    got = [(int(v), int(r)) for v, r in re.findall(r"ff: isqrt (\d+) (\d+)", log)]
    check("isqrt32 (fixmath.h): floor(sqrt) up to 2^32 - 1", len(got) == 10 and all(r == math.isqrt(v) for v, r in got),
          str([g for g in got if g[1] != math.isqrt(g[0])]))
    m = re.search(r"ff: fix16 ([0-9A-F]{8})", log)
    hx, hy = fix16_hash()
    want = f"{hx:08X}"
    check(f"fix16: fmul, fdiv, a3_angle, fhypot, fsqrt, fsin over 2000 pairs = the 64-bit model ({want})",
          m is not None and m.group(1) == want, m.group(1) if m else "missing")
    m = re.search(r"ff: fix16x ([0-9A-F]{8})", log)
    want = f"{hy:08X}"
    check(f"fix16: fatan2, a3_atan2, fasin, fhypot3, fexp_neg over 1000 pairs = the amiga3d engine's ({want})",
          m is not None and m.group(1) == want, m.group(1) if m else "missing")

    m = re.search(r"ff: sin ([0-9A-F]{8})", log)
    h = 2166136261
    for a in range(65536):
        for b in (a, (a + 16384) & 0xFFFF):
            i, f = b >> 8, b & 255
            v = SIN[i] + (((SIN[(i + 1) & 255] - SIN[i]) * f) >> 8)
            h = ((h ^ (v & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
    want = f"{h:08X}"
    check(f"fix16: a3_sin and a3_cos at every angle = the engine's interpolation ({want})",
          m is not None and m.group(1) == want, m.group(1) if m else "missing")

print(f"{'FAIL' if failures else 'PASS'}: fix16test")
sys.exit(1 if failures else 0)
