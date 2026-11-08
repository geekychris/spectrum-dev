"""The original's course files (course.c's loader in Python).

course<N>.bin is big-endian int32 throughout (tools/courses.py of the 3DO
version wrote it); a Course has the same fields as rs.h's Course.
"""
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
ORIG = HERE / "orig"                       # the original's files (tools/orig/README)
DATA = ORIG / "data"
FX = 4096
NAMES = ["WARM-UP", "FIRST DROP", "SWITCHBACK", "SKYWAY", "TOPSY-TURVY", "THE GAUNTLET"]


class Course:
    pass


def load(index, data=DATA):
    b = (Path(data) / f"course{index}.bin").read_bytes()
    w = struct.unpack(f">{len(b) // 4}i", b)
    assert w[0] == 0x52534331
    pos = [9]

    def take():
        v = w[pos[0]]
        pos[0] += 1
        return v

    def tn(n):
        v = w[pos[0]:pos[0] + n]
        pos[0] += n
        return list(v)

    c = Course()
    c.index = index
    c.name = b[4:36].split(b"\0")[0].decode()
    c.time_ms, c.gold_ms, c.silver_ms, c.bronze_ms, c.decor, c.music = tn(6)
    c.spawn = tn(3)
    c.goal = tn(8)                         # cx cy cz hx hy hz cs sn
    c.goal_world = tn(3)
    c.kill_y = take()
    nv = take()
    c.v = [tuple(w[pos[0] + 3 * i:pos[0] + 3 * i + 3]) for i in range(nv)]
    pos[0] += 3 * nv
    nq = take()
    c.q = []                               # (v[4], col[3] words, flags, nrm)
    for i in range(nq):
        r = tn(9)
        c.q.append((r[0:4], [x & 0xFFFFFFFF for x in r[4:7]], r[7], r[8]))
    nt = take()
    c.t = []                               # (v[3], n[3] Q14, flags)
    for i in range(nt):
        r = tn(7)
        c.t.append((r[0:3], r[3:6], r[6]))

    def grid(spheres):
        g = {}
        g["x0"], g["z0"], g["cell"], g["nx"], g["nz"] = tn(5)
        n = g["nx"] * g["nz"]
        g["off"] = tn(n + 1)
        g["idx"] = tn(g["off"][n])
        g["sphere"] = tn(4 * n) if spheres else None
        return g

    c.cgrid = grid(False)
    c.rgrid = grid(True)
    npath = take()
    c.path = [tn(5) for _ in range(npath)]  # x y z safe progress
    for name, size in (("acid", 8), ("zone", 13), ("pillar", 5), ("sweep", 6), ("crush", 9),
                       ("enemy", 8), ("crumble", 4)):
        n = take()
        setattr(c, name, [tn(size) for _ in range(n)])
    n = take()
    c.deco = []
    for i in range(n):
        d = dict(zip(("x", "y", "z", "scale", "phase", "amp", "speed", "spin", "yaw"), tn(9)))
        dnv = take()
        d["v"] = [tuple(w[pos[0] + 3 * k:pos[0] + 3 * k + 3]) for k in range(dnv)]
        pos[0] += 3 * dnv
        dnq = take()
        d["q"] = [tn(7) for _ in range(dnq)]
        dns = take()
        d["s"] = [tn(5) for _ in range(dns)]
        c.deco.append(d)
    c.deco = c.deco[:16]                   # MAXDECOR
    c.crumble = c.crumble[:32]
    assert pos[0] == len(w), (pos[0], len(w))
    return c
