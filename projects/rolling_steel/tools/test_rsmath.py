"""The model of src/rsmath_test.asm: the same operands and C's 32-bit
results, hashed the same way. python3 tools/test_rsmath.py [n]"""
import math
import sys

M = 0xFFFFFFFF


def s32(v):
    v &= M
    return v - (1 << 32) if v & 0x80000000 else v


def s16(v):
    v &= 0xFFFF
    return v - 65536 if v & 0x8000 else v


def mix(h, w):
    return ((h ^ (w & M)) * 16777619) & M


def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a < 0) == (b < 0) else -q


def selftest(n):
    h, s = 2166136261, 0x12342545
    def rand():
        nonlocal s
        s ^= (s << 13) & M
        s ^= s >> 17
        s ^= (s << 5) & M
        return s
    for _ in range(n):
        a, b, r = s32(rand()), s32(rand()), rand()
        a >>= r & 31
        b >>= (r >> 8) & 31
        h = mix(h, a * b)
        h = mix(h, a * s16(b))
        h = mix(h, s16(a) * s16(b))
        if b:
            h = mix(h, cdiv(a, b))
        h = mix(h, a >> (b & 31))
        h = mix(h, a << (b & 31))
        for v in (a >> 4, a >> 12, a << 4, a << 12):
            h = mix(h, v)
        h = mix(h, math.isqrt(a & M))
    return h


if __name__ == "__main__":
    print(f"{selftest(int(sys.argv[1]) if len(sys.argv) > 1 else 2000):08X}")
