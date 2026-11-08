"""Each course drawn whole, as the original's camera sees it (rsview.py),
then cut into what the Next needs. Run: python3 tools/gen_course.py"""
import math
import struct
import sys
import numpy as np
import os
import rscourse
import rsview
from rsview import FX

HERE_DIR = os.path.dirname(os.path.abspath(__file__))
BG = (1 << 10) | (1 << 5) | 3            # render_begin(): the void the courses float in


def path_points(c, cam, step=FX // 2):
    """the route, every half unit: (screen x, y in pixels, depth)"""
    pts = []
    for a, b in zip(c.path, c.path[1:]):
        n = max(1, int(max(abs(b[0] - a[0]), abs(b[1] - a[1]), abs(b[2] - a[2])) // step))
        for k in range(n):
            p = [a[i] + (b[i] - a[i]) * k // n for i in range(3)]
            x, y, d = cam.proj(p)
            pts.append((x / 65536, y / 65536, d))
    return np.array(pts)


def faces(c, cam):
    """course quads facing the camera: (xs, ys, zs, key, quad index)"""
    out = []
    for qi, (vs, col, flags, nw) in enumerate(c.q):
        nx = ((nw << 2) & 0xFFFFFFFF)
        nx = (nx - (1 << 32) if nx & 0x80000000 else nx) >> 22
        ny = ((nw << 12) & 0xFFFFFFFF)
        ny = (ny - (1 << 32) if ny & 0x80000000 else ny) >> 22
        nz = ((nw << 22) & 0xFFFFFFFF)
        nz = (nz - (1 << 32) if nz & 0x80000000 else nz) >> 22
        if nx * cam.f[0] + ny * cam.f[1] + nz * cam.f[2] >= 0:
            continue
        P = [cam.proj(c.v[i]) for i in vs]
        xs, ys, zs = [p[0] for p in P], [p[1] for p in P], [p[2] for p in P]
        cr = ((xs[1] - xs[0]) >> 8) * ((ys[3] - ys[0]) >> 8) - ((ys[1] - ys[0]) >> 8) * ((xs[3] - xs[0]) >> 8)
        if cr <= 0:
            continue
        if (flags & 255) == 2:
            zs = [z - 205 for z in zs]
        out.append((xs, ys, zs, max(p[2] for p in P), qi))
    return out


def render(c, cam=None):
    cam = cam or rsview.Cam()
    fl = faces(c, cam)
    xs = [x for f in fl for x in f[0]]
    ys = [y for f in fl for y in f[1]]
    ox, oy = (min(xs) >> 16) - 8, (min(ys) >> 16) - 8
    w, h = (max(xs) >> 16) - ox + 9, (max(ys) >> 16) - oy + 9
    cv = rsview.Canvas(w, h, ox, oy, BG)
    pp = path_points(c, cam)
    for xs_, ys_, zs, key, qi in fl:
        vs, col, flags, nw = c.q[qi]
        if flags >> 8:
            continue                       # crumbling tiles: sprites on the Next
        cx, cy = sum(xs_) / 4 / 65536, sum(ys_) / 4 / 65536
        k = np.argmin((pp[:, 0] - cx) ** 2 + (pp[:, 1] - cy) ** 2)
        rel = key - pp[k, 2]               # port: the fog's depth from the route nearest on screen
        lv = int(rel - 4 * FX) >> 14
        cv.quad(xs_, ys_, zs, rsview.fog_col(col, lv), qi + 1)
    decor(c, cam, cv, pp)
    return cv


SPHERES = {}


def decor(c, cam, cv, pp):
    """render.c's decor() at clock 0. port: the floating scenery holds still
    (the original bobs and turns it); it is in the picture"""
    for d in c.deco:
        if len(d["v"]) > 256:
            continue
        sc = d["scale"]
        ang = (d["phase"] * 10430) >> 12
        bob = (rsview.isin(ang) * (d["amp"] >> 2)) >> 12
        yaw = rsview.c_div(d["yaw"] * 2, 45)
        cs, sn = rsview.icos(yaw) >> 2, rsview.isin(yaw) >> 2
        pos = (d["x"], d["y"] + bob, d["z"])
        P = []
        for v in d["v"]:
            lx, ly, lz = (v[0] * sc) >> 12, (v[1] * sc) >> 12, (v[2] * sc) >> 12
            P.append(cam.proj((pos[0] + ((lx * cs + lz * sn) >> 12), pos[1] + ly, pos[2] + ((-lx * sn + lz * cs) >> 12))))
        for q in d["q"]:
            xs = [P[k][0] for k in q[:4]]
            ys = [P[k][1] for k in q[:4]]
            zs = [P[k][2] for k in q[:4]]
            cr = ((xs[1] - xs[0]) >> 8) * ((ys[3] - ys[0]) >> 8) - ((ys[1] - ys[0]) >> 8) * ((xs[3] - xs[0]) >> 8)
            if cr <= 0:
                continue
            key = max(zs)
            k = np.argmin((pp[:, 0] - sum(xs) / 4 / 65536) ** 2 + (pp[:, 1] - sum(ys) / 4 / 65536) ** 2)
            lv = int(key - pp[k, 2] - 4 * FX) >> 14
            cv.quad(xs, ys, zs, rsview.fog_col([x & 0xFFFFFFFF for x in q[4:7]], lv), 30000)
        for s in d["s"]:
            lx, lz = (s[0] * sc) >> 12, (s[2] * sc) >> 12
            w = (pos[0] + ((lx * cs + lz * sn) >> 12), pos[1] + ((s[1] * sc) >> 12), pos[2] + ((-lx * sn + lz * cs) >> 12))
            r = (s[3] * sc) >> 12
            look = s[4] if s[4] < 8 else rsview.SP_CRYSTAL
            if look not in SPHERES:
                SPHERES[look] = rsview.sphere(look)
            x, y, dz = cam.proj(w)
            cv.sprite(SPHERES[look], x, y, (r * cam.scale) >> 4, dz - r, 30001)


# ---- the physics' data (phys.asm, course_c.c) ----
PAGE = 8192


TRI_SIZE = 46
TRI_PER_PAGE = PAGE // TRI_SIZE          # 178


def i24(v):
    assert -(1 << 23) <= v < (1 << 23), v
    return struct.pack("<i", v)[:3]


def tri_records(c):
    """46-byte triangles as phys.asm reads them, 178 to a page: the three
    vertices (24-bit), the normal as course.c leaves it (Q14 >> 2), group,
    class, and the bounding sphere course.c works out at load (integer for
    integer): centre (24-bit), radius (16-bit)"""
    out = bytearray()
    for k, (vs, n, flags) in enumerate(c.t):
        if k and k % TRI_PER_PAGE == 0:
            out += bytes(-len(out) % PAGE)
        P = [c.v[i] for i in vs]
        rec = b"".join(i24(x) for p in P for x in p)
        rec += struct.pack("<3h", *[v >> 2 for v in n])
        rec += bytes([(flags >> 8) & 255, flags & 255])
        sp = [rsview.c_div(sum(p[i] for p in P), 3) for i in range(3)]
        r = 0
        for p in P:
            dx, dy, dz = ((p[i] - sp[i]) >> 4 for i in range(3))
            r = max(r, math.isqrt(dx * dx + dy * dy + dz * dz) << 4)
        assert r + 16 < 65536
        rec += b"".join(i24(x) for x in sp) + struct.pack("<H", r + 16)
        assert len(rec) == TRI_SIZE
        out += rec
    return out


def tri_ref(i):
    """a triangle's number in the grid: its page (from the first) << 8 | its slot"""
    return (i // TRI_PER_PAGE) << 8 | (i % TRI_PER_PAGE)


def pad_page(b):
    return b + bytes(-len(b) % PAGE)


def course_blob(c):
    """one course's physics: a header page (the small tables), the
    triangles, the grid's off[] and idx[] (each from a page boundary)"""
    tris = pad_page(tri_records(c))
    g = c.cgrid
    off = pad_page(struct.pack(f"<{len(g['off'])}H", *g["off"]))
    idx = pad_page(struct.pack(f"<{len(g['idx'])}H", *[tri_ref(i) for i in g["idx"]]))
    tp = 1
    gp = tp + len(tris) // PAGE
    ip = gp + len(off) // PAGE
    h = struct.pack("<HHiiHHBBB", len(c.t), len(c.path), g["x0"], g["z0"], g["nx"], g["nz"], tp, gp, ip)
    h += struct.pack("<5i", c.time_ms, c.gold_ms, c.silver_ms, c.bronze_ms, c.music)
    h += struct.pack("<7i", *c.spawn, *c.goal_world, c.kill_y)
    h += struct.pack("<8i", *c.goal)
    lists = [c.acid, c.zone, c.pillar, c.sweep, c.crush, c.enemy, c.crumble]
    h += bytes(len(l) for l in lists) + b"\0"
    for l in lists:
        for r in l:
            h += struct.pack(f"<{len(r)}i", *r)
    for r in c.path:
        h += struct.pack("<5i", *r)
    h += c.name.encode().ljust(33, b"\0")
    assert len(h) <= PAGE
    return pad_page(h) + tris + off + idx


# ---- the picture (view.c, l2scroll.asm) ----
def rgb333(c):
    """a 15-bit colour as the Next's 9-bit RRRGGGBBB"""
    r, g, b = rsview.rgb15(c)
    return (round(r * 7 / 31) << 6) | (round(g * 7 / 31) << 3) | round(b * 7 / 31)


def runs_of(line):
    """(length 1..255, colour) runs of a sequence of colour indices"""
    out = bytearray()
    i, n = 0, len(line)
    while i < n:
        j = i
        while j < n and j - i < 255 and line[j] == line[i]:
            j += 1
        out += bytes([j - i, int(line[i])])
        i = j
    return out


def picture(cv):
    """a course picture as palette indices, run-length coded down each
    column and along each row (the Next fills new columns from the one and
    new rows from the other as it scrolls), with the palette"""
    cols9 = np.vectorize(rgb333)(cv.col)
    pal = sorted(set(int(v) for v in np.unique(cols9)))
    assert len(pal) < 0xE3, len(pal)          # 0xE3 is Layer 2's transparent colour
    lut = {v: i for i, v in enumerate(pal)}
    idx = np.vectorize(lut.get)(cols9).astype(np.uint8)
    h, w = idx.shape
    void = lut[rgb333(BG)]
    head = struct.pack("<HHhhBB", w, h, cv.ox, cv.oy, void, len(pal))
    head += struct.pack(f"<{len(pal)}H", *pal)
    dir_at = len(head)
    data_at = dir_at + 3 * (w + h)
    body = bytearray()
    dirs = bytearray()
    for line in [idx[:, x] for x in range(w)] + [idx[y, :] for y in range(h)]:
        o = data_at + len(body)
        dirs += bytes([o >> 13]) + struct.pack("<H", o & 0x1FFF)
        body += runs_of(line)
    return pad_page(head + dirs + body), idx, pal


FIRST_PAGE = 36                          # 8K page; below: program, Layer 2, code banks (main.c)
LAST_PAGE = 223                          # a 2 MB Next


def build(out_dir):
    """all six courses' physics and pictures into 16K banks gfx/bank_NN.bin,
    src/data_banks.asm (the .nex puts them there) and src/data.h"""
    from pathlib import Path
    out_dir = Path(out_dir)
    blob = bytearray()
    cpage, ppage, info = [], [], []
    for i in range(1, 7):
        c = rscourse.load(i)
        cpage.append(FIRST_PAGE + len(blob) // PAGE)
        blob += course_blob(c)
        cv = render(c)
        pic, idx, pal = picture(cv)
        ppage.append(FIRST_PAGE + len(blob) // PAGE)
        blob += pic
        info.append((c.name, cv.w, cv.h, len(pal)))
    blob += bytes(-len(blob) % (2 * PAGE))
    last = FIRST_PAGE + len(blob) // PAGE - 1
    assert last <= LAST_PAGE, last
    gfx = out_dir / "gfx"
    gfx.mkdir(exist_ok=True)
    for f in gfx.glob("bank_*.bin"):
        f.unlink()
    asm = ["; made by tools/gen_course.py: the courses (physics, pictures), straight into banks", ""]
    for k in range(0, len(blob), 2 * PAGE):
        bank = (FIRST_PAGE * PAGE + k) // (2 * PAGE)
        (gfx / f"bank_{bank}.bin").write_bytes(blob[k:k + 2 * PAGE])
        asm += [f"SECTION BANK_{bank}", f'BINARY "gfx/bank_{bank}.bin"']
    (out_dir / "src" / "data_banks.asm").write_text("\n".join(asm) + "\n")
    h = ["/* made by tools/gen_course.py */", "#ifndef DATA_H", "#define DATA_H", "#include <stdint.h>",
         f"static const uint8_t course_page[6] = {{ {', '.join(map(str, cpage))} }};   /* physics: header page */",
         f"static const uint8_t pic_page[6] = {{ {', '.join(map(str, ppage))} }};      /* pictures */",
         f"#define DATA_LAST_PAGE {last}", "#endif", ""]
    (out_dir / "src" / "data.h").write_text("\n".join(h))
    for (name, w, h_, n), cp, pp in zip(info, cpage, ppage):
        print(f"  {name:12} physics page {cp}, picture page {pp}: {w} x {h_}, {n} colours")
    print(f"  pages {FIRST_PAGE}..{last}")


if __name__ == "__main__":
    import os
    build(os.path.dirname(HERE_DIR))
