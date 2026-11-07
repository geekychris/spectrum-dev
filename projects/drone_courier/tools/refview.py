#!/usr/bin/env python3
"""The original's 3D view (render.c scene(): camera, cull, cam_xform,
clip_near, a3_poly's projection, the 512 depth buckets, far to near) in
Python, for checking the Next's view.c against it: test.py dumps a game
state from the emulator, renders it here at the original's 320 x 256 and at
the port's 4/5 scale, 96 rows, and compares with Layer 2's back buffer.

  render(state, cam_yaw, scale=1.0, rows=256, half=False) -> PIL image (pen indices)
  state: dict with the DCGame fields used (see parse_dcgame)
"""
import struct

DC_N, DC_PITCH, DC_FOOT = 12, 160, 55
DC_HALF = DC_N * DC_PITCH // 2
NEAR_Z = 12
RANGE = 440                      # view.c RANGE (render.c view_range: 560 on the 68k)
PEN_SKY, PEN_GROUND, PEN_PARK, PEN_TOWER, PEN_ROOF = 4, 5, 6, 7, 13
PEN_DEPOT, PEN_CUST, PEN_BEACON, PEN_DRONE, PEN_ROTOR = 14, 15, 16, 17, 19
PEN_PARCEL, PEN_FRAGILE, PEN_SHADOW = 20, 21, 22

SIN = None


def _sin_tab():
    import re, os
    src = open(os.environ.get("A3D", "/Users/chris/code/claude_world/amiga_mcp/examples/amiga3d") + "/a3d_math.c").read()
    a = src.index("a3_sin_tab[256]")
    body = src[src.index("{", a) + 1:src.index("};", a)]
    return [int(v) for v in re.findall(r"-?\d+", body)]


def tdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def a3_sin(a):
    global SIN
    if SIN is None:
        SIN = _sin_tab()
    a &= 0xFFFF
    i, f = (a >> 8) & 255, a & 255
    s0, s1 = SIN[i], SIN[(i + 1) & 255]
    return s0 + (((s1 - s0) * f) >> 8)


def a3_cos(a):
    return a3_sin(a + 16384)


def parse_dcgame(b):
    """DCGame as zsdcc lays it out (int = 16 bits)"""
    o = 0
    def take(fmt):
        nonlocal o
        v = struct.unpack_from("<" + fmt, b, o)
        o += struct.calcsize("<" + fmt)
        return v
    g = {}
    g["seed"], g["rng"] = take("II")
    g["day"], g["over"], g["won"], g["between"] = take("hhhh")
    h = take("144h")
    g["height"] = [list(h[r * 12:(r + 1) * 12]) for r in range(12)]
    g["x"], g["y"], g["z"], g["vx"], g["vy"], g["vz"] = take("6i")
    g["yaw"], = take("i")
    (g["carrying"], g["fragile"], g["depot"], g["target"], g["jobs"], g["jobs_done"], g["fails"],
     g["pad_r"]) = take("8h")
    g["battery"], g["wind_x"], g["wind_z"], g["gust_x"], g["gust_z"] = take("5i")
    g["gust_timer"], = take("h")
    g["job_time"], g["tick"], g["score"] = take("3i")
    return g


def ground(g, x, z):
    """dc_ground(), whole units: the roof under (x, z) (x, z 16.16)"""
    ix, iz = (x >> 16) + DC_HALF, (z >> 16) + DC_HALF
    if ix < 0 or iz < 0:
        return 0
    c, r = ix // DC_PITCH, iz // DC_PITCH
    if c >= DC_N or r >= DC_N:
        return 0
    if abs(x - ((-DC_HALF + c * DC_PITCH + DC_PITCH // 2) << 16)) >= DC_FOOT << 16:
        return 0
    if abs(z - ((-DC_HALF + r * DC_PITCH + DC_PITCH // 2) << 16)) >= DC_FOOT << 16:
        return 0
    return g["height"][r][c]


def a3_atan2(x, y):
    import math
    return int(round(math.atan2(x, y) * 65536 / (2 * math.pi))) & 0xFFFF   # (close enough for a picture)


class View:
    def __init__(self, g, cam_yaw, fov, cx, cy, w, h):
        fx, fz = a3_sin(cam_yaw), a3_cos(cam_yaw)
        self.x = (g["x"] >> 16) - ((fx * 170) >> 16)
        self.z = (g["z"] >> 16) - ((fz * 170) >> 16)
        self.y = (g["y"] >> 16) + 70
        gy = ground(g, self.x << 16, self.z << 16) + 20
        if self.y < gy:
            self.y = gy
        self.yaw = cam_yaw
        p = a3_atan2(self.y - ground(g, g["x"], g["z"]), 300)
        self.pitch = min(max(p, 1200), 9000)
        self.cy, self.sy = a3_cos(self.yaw), a3_sin(self.yaw)
        self.cp, self.sp = a3_cos(self.pitch), a3_sin(self.pitch)
        self.fov, self.cx, self.cyc, self.w, self.h = fov, cx, cy, w, h
        self.polys = []

    def xform(self, wx, wy, wz):
        dx, dz, dy = wx - self.x, wz - self.z, wy - self.y
        rx = (dx * (self.cy >> 4) - dz * (self.sy >> 4)) >> 12
        rz = (dx * (self.sy >> 4) + dz * (self.cy >> 4)) >> 12
        return (rx, (dy * (self.cp >> 4) + rz * (self.sp >> 4)) >> 12, (rz * (self.cp >> 4) - dy * (self.sp >> 4)) >> 12)

    def quad(self, pts, bias, col):
        v = [self.xform(*p) for p in pts]
        if all(p[2] < NEAR_Z for p in v):
            return
        if all(p[0] > p[2] for p in v) or all(p[0] < -p[2] for p in v):
            return
        depth = ((v[0][2] + v[1][2] + v[2][2] + v[3][2]) >> 2) + bias
        out = []
        for i in range(4):
            a, b = v[i], v[(i + 1) % 4]
            if a[2] >= NEAR_Z:
                out.append(a)
            if (a[2] >= NEAR_Z) != (b[2] >= NEAR_Z):
                p, q = (a, b) if a[2] >= NEAR_Z else (b, a)
                t, dz = NEAR_Z - p[2], q[2] - p[2]
                out.append((p[0] + tdiv((q[0] - p[0]) * t, dz), p[1] + tdiv((q[1] - p[1]) * t, dz), NEAR_Z))
        if len(out) < 3:
            return
        xy = [(self.cx + p[0] * self.fov / p[2], self.cyc - p[1] * self.fov / p[2]) for p in out]
        b = 0 if depth < 0 else 511 if depth >= 4000 else depth * 512 // 4000
        self.polys.append((b, len(self.polys), col, xy))

    def flat(self, x0, x1, z0, z1, y, bias, col):
        self.quad([(x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1)], bias, col)


def render(g, cam_yaw, scale=1.0, rows=256, half=False, drone=True):
    from PIL import Image, ImageDraw
    k = scale
    fov, cx, cy = 200 * k, 160 * k, 110 * k
    w = int(320 * k)
    if half:
        fov_y = fov / 2
    v = View(g, cam_yaw, fov, cx, cy, w, rows)
    # the city
    fx, fz = a3_sin(v.yaw), a3_cos(v.yaw)
    c0, c1 = max(0, int((v.x - RANGE + DC_HALF) / DC_PITCH)), min(DC_N - 1, int((v.x + RANGE + DC_HALF) / DC_PITCH))
    r0, r1 = max(0, int((v.z - RANGE + DC_HALF) / DC_PITCH)), min(DC_N - 1, int((v.z + RANGE + DC_HALF) / DC_PITCH))
    goal = g["target"] if g["carrying"] else g["depot"]
    for r in range(r0, r1 + 1):
        for c in range(c0, c1 + 1):
            bx, bz = -DC_HALF + c * DC_PITCH + DC_PITCH // 2, -DC_HALF + r * DC_PITCH + DC_PITCH // 2
            dx, dz, h = bx - v.x, bz - v.z, g["height"][r][c]
            if ((dx * fx + dz * fz) >> 16) < -DC_PITCH:
                continue
            if abs(dx) > RANGE or abs(dz) > RANGE:
                continue
            if h == 0:
                v.flat(bx - DC_FOOT, bx + DC_FOOT, bz - DC_FOOT, bz + DC_FOOT, 1, 60, PEN_PARK)
                continue
            x0, x1, z0, z1 = bx - DC_FOOT, bx + DC_FOOT, bz - DC_FOOT, bz + DC_FOOT
            tone = (r * 7 + c * 3) % 3
            lit, sh = PEN_TOWER + 2 * tone, PEN_TOWER + 2 * tone + 1
            v.flat(x0, x1, z0, z1, h, 0, PEN_ROOF)
            if v.z < z0:
                v.quad([(x0, 0, z0), (x1, 0, z0), (x1, h, z0), (x0, h, z0)], 0, lit)
            elif v.z > z1:
                v.quad([(x0, 0, z1), (x1, 0, z1), (x1, h, z1), (x0, h, z1)], 0, lit)
            if v.x < x0:
                v.quad([(x0, 0, z0), (x0, 0, z1), (x0, h, z1), (x0, h, z0)], 0, sh)
            elif v.x > x1:
                v.quad([(x1, 0, z0), (x1, 0, z1), (x1, h, z1), (x1, h, z0)], 0, sh)
            b = r * DC_N + c
            if b == g["depot"] or b == goal:
                pr = g["pad_r"]
                pen = PEN_DEPOT if b == g["depot"] else PEN_CUST
                v.flat(bx - pr, bx + pr, bz - pr, bz + pr, h + 1, -4, pen)
                v.flat(bx - pr + 6, bx + pr - 6, bz - pr + 6, bz + pr - 6, h + 2, -5, PEN_ROOF)
                v.flat(bx - pr + 12, bx + pr - 12, bz - pr + 12, bz + pr - 12, h + 3, -6, pen)
    # the beacon
    b = goal
    bx, bz = -DC_HALF + (b % DC_N) * DC_PITCH + DC_PITCH // 2, -DC_HALF + (b // DC_N) * DC_PITCH + DC_PITCH // 2
    h, wd = g["height"][b // DC_N][b % DC_N], 3 + (g["tick"] & 2)
    v.quad([(bx - wd, h, bz), (bx + wd, h, bz), (bx + wd, h + 260, bz), (bx - wd, h + 260, bz)], -20,
           PEN_CUST if g["carrying"] else PEN_DEPOT)
    v.quad([(bx, h, bz - wd), (bx, h, bz + wd), (bx, h + 260, bz + wd), (bx, h + 260, bz - wd)], -20, PEN_BEACON)
    # the drone (its shadow always: the Next draws only that in Layer 2)
    x, y, z = g["x"] >> 16, g["y"] >> 16, g["z"] >> 16
    gy = ground(g, g["x"], g["z"])
    fx, fz = a3_sin(g["yaw"]), a3_cos(g["yaw"])
    rx, rz = fz, -fx
    def P(a, bb, yy):
        return (x + ((fx * a + rx * bb) >> 16), yy, z + ((fz * a + rz * bb) >> 16))
    v.flat(x - 14, x + 14, z - 14, z + 14, gy + 1, 30, PEN_SHADOW)
    if drone:
        v.quad([P(-10, -8, y + 4), P(-10, 8, y + 4), P(10, 8, y + 4), P(10, -8, y + 4)], -2, PEN_DRONE)
        v.quad([P(10, -8, y), P(10, 8, y), P(10, 8, y + 4), P(10, -8, y + 4)], -1, PEN_DRONE + 1)
        v.quad([P(-10, -8, y), P(-10, 8, y), P(-10, 8, y + 4), P(-10, -8, y + 4)], -1, PEN_DRONE + 1)
        for kk in range(4):
            a, bb, s = (16 if kk & 1 else -16), (16 if kk & 2 else -16), 7 + ((g["tick"] + kk) & 1)
            v.quad([P(a - s, bb - s, y + 6), P(a - s, bb + s, y + 6), P(a + s, bb + s, y + 6),
                    P(a + s, bb - s, y + 6)], -3, PEN_ROTOR)
        if g["carrying"]:
            col = PEN_FRAGILE if g["fragile"] else PEN_PARCEL
            v.quad([P(-6, -6, y - 9), P(-6, 6, y - 9), P(6, 6, y - 9), P(6, -6, y - 9)], -1, col)
            v.quad([P(-6, -6, y - 9), P(-6, 6, y - 9), P(-6, 6, y), P(-6, -6, y)], 0, col)
            v.quad([P(6, -6, y - 9), P(6, 6, y - 9), P(6, 6, y), P(6, -6, y)], 0, col)
    # sky, ground, then far to near
    hz = int(v.cyc - (v.fov * (v.sp >> 4)) / (v.cp >> 4))
    img = Image.new("P", (w, int(256 * k)), PEN_GROUND)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, w, max(hz, 1) - 1], fill=PEN_SKY)
    for bkt, i, col, xy in sorted(v.polys, key=lambda p: (-p[0], p[1])):
        d.polygon(xy, fill=col)
    if half:
        img = img.crop((0, 0, w, rows * 2)).resize((w, rows), Image.NEAREST)
    return img, v
