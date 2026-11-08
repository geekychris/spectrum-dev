"""The original's view (render.c) on the host: its camera, projection, fog,
lighting and sphere sprites, rasterised with a depth buffer (as the Amiga
port's softcel.c does) into one big picture of a whole course.

port: the Next can't project ~900 faces a frame, so each course is drawn
here once, at the original's default view (yaw 0, pitch 35 degrees, size
10.5: 11.4 pixels a unit), and the Next scrolls that picture.
"""
import math
import numpy as np

FX = 4096
QSIN = [0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897,
        6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297,
        11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978,
        15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384]


def isin(a):
    a &= 0xFFFF
    q, i, f = a >> 14, (a >> 8) & 63, a & 255
    if q & 1:
        v0, v1 = QSIN[64 - i], QSIN[63 - i]
    else:
        v0, v1 = QSIN[i], QSIN[i + 1]
    v = v0 + (((v1 - v0) * f) >> 8)
    return -v if q & 2 else v


def icos(a):
    return isin(a + 16384)


def deg(d):
    return int(d * 65536.0 / 360.0)


def c_div(a, b):
    """C's / (truncates towards zero)"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


class Cam:
    """cam_basis(): yaw 0, pitch 35 degrees, size 10.5 (game.c's defaults)"""
    def __init__(self, yaw=0, pitch=deg(35), size=int(10.5 * 4096)):
        sy, cy, sp, cp = isin(yaw) >> 2, icos(yaw) >> 2, isin(pitch) >> 2, icos(pitch) >> 2
        self.r = (cy, 0, -sy)
        self.u = ((sp * sy) >> 12, cp, (sp * cy) >> 12)
        self.f = ((cp * sy) >> 12, -sp, (cp * cy) >> 12)
        self.scale = c_div(120 << 16, size >> 4)          # px per unit, Q8

    def proj(self, p):
        """render.c's proj() with the focus at 0: screen x, y (16.16) and depth (Q12)"""
        rx, ry, rz = p[0] >> 2, p[1] >> 2, p[2] >> 2
        r, u, f = self.r, self.u, self.f
        x = (rx * r[0] + rz * r[2]) >> 10
        y = (rx * u[0] + ry * u[1] + rz * u[2]) >> 10
        dz = (rx * f[0] + ry * f[1] + rz * f[2]) >> 10
        return (x * self.scale) >> 4, -((y * self.scale) >> 4), dz


# ---- lighting (render.c) ----
SRGB_THR = [5, 16, 30, 50, 76, 109, 148, 195, 250, 313, 385, 465, 554, 652, 759, 877, 1004, 1142, 1289, 1448,
            1617, 1798, 1989, 2192, 2407, 2633, 2871, 3122, 3384, 3660, 3947]
SRGB5 = []
_k = 0
for _i in range(1025):
    while _k < 31 and (_i << 2) >= SRGB_THR[_k]:
        _k += 1
    SRGB5.append(_k)


def lin5(v):
    v >>= 2
    return SRGB5[max(0, min(1024, v))]


L_DANGER = ((1954, 136, 111), (163, 10, 6), 1434)


def shade(m, nx, ny, nz):
    fill = (700, 1081, 2843)
    k = max(0, (nx * 1261 + ny * 3228 - nz * 2184) >> 12)
    f = max(0, (-nx * 1316 + ny * 1401 + nz * 3617) >> 12)
    a, e, metal = m
    c = []
    for i in range(3):
        light = 246 + ((2867 * k) >> 12) + ((((1229 * fill[i]) >> 12) * f) >> 12) + (c_div(ny, 2) if ny > 0 else 0)
        v = ((((a[i] * light) >> 12) * (FX - ((3 * metal) >> 2))) >> 12) + ((a[i] * ((metal * 901) >> 12)) >> 12) + e[i]
        c.append(lin5(v))
    i = (c[0] << 10) | (c[1] << 5) | c[2]
    return i if i else 1


SP_MARBLE, SP_MARBLE2, SP_STEEL, SP_BLOB, SP_GHOST, SP_CRYSTAL, SP_CANDY, SP_GLOW = range(8)
ALB = [(2910, 3066, 3474), (4096, 1954, 603), (364, 388, 516), (388, 3559, 544),
       (1078, 3645, 4096), (1078, 2614, 3645), (4096, 489, 1610), (4096, 1610, 261)]
EMIS = [(0, 0, 0), (0, 0, 0), (0, 0, 0), (35, 603, 48), (300, 600, 700), (16, 80, 163), (544, 20, 163), (1305, 300, 20)]
METAL = [1, 1, 1, 0, 0, 0, 0, 0]


def isqrt(v):
    return math.isqrt(v)


def sphere(look, S=32):
    """make_sphere(): the 32 x 32 shaded sphere sprite, 15-bit colours (0 = clear)"""
    alb, emis, metal, rim = ALB[look], EMIS[look], METAL[look], look == SP_GHOST
    px = np.zeros((S, S), np.int32)
    for j in range(S):
        for i in range(S):
            nx = c_div((i * 2 + 1 - S) * 4096, S)
            ny = c_div(-(j * 2 + 1 - S) * 4096, S)
            d2 = (nx * nx + ny * ny) >> 12
            if d2 >= 4096:
                continue
            nz = isqrt((4096 - d2) << 12)
            k = max(0, (nx * 1261 + ny * 1388 + nz * 3641) >> 12)
            spec = min(4096, (k - 3700) * 10 if k > 3700 else 0)
            c = []
            for ch in range(3):
                light = 330 + ((2600 * k) >> 12) + (c_div(ny, 4) if ny > 0 else 0)
                val = (alb[ch] * light) >> 12
                val += (spec * 3800) >> 12 if metal else (spec * 900) >> 12
                val += emis[ch]
                cc = lin5(val)
                if rim and d2 > 3300:
                    cc = cc * 2 // 3
                c.append(cc)
            v = (c[0] << 10) | (c[1] << 5) | c[2]
            px[j, i] = v if v else 1
    return px


def rgb15(c):
    return ((c >> 10) & 31, (c >> 5) & 31, c & 31)


# ---- the rasteriser ----
class Canvas:
    """15-bit colour + depth (smaller = nearer) over a pixel rectangle; corner
    coordinates are 16.16 screen units minus (ox, oy) pixels"""
    def __init__(self, w, h, ox, oy, bg):
        self.w, self.h, self.ox, self.oy = w, h, ox, oy
        self.col = np.full((h, w), bg, np.int32)
        self.z = np.full((h, w), np.inf)
        self.tag = np.zeros((h, w), np.int32)        # what drew each pixel (caller's id)

    def tri(self, xs, ys, zs, col, tag=0, zbias=0.0):
        x = [v / 65536.0 - self.ox for v in xs]
        y = [v / 65536.0 - self.oy for v in ys]
        x0, x1 = max(0, int(math.floor(min(x)))), min(self.w - 1, int(math.ceil(max(x))))
        y0, y1 = max(0, int(math.floor(min(y)))), min(self.h - 1, int(math.ceil(max(y))))
        if x0 > x1 or y0 > y1:
            return
        area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0])
        if abs(area) < 1e-9:
            return
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        w0 = ((x[2] - x[1]) * (gy - y[1]) - (y[2] - y[1]) * (gx - x[1])) / area
        w1 = ((x[0] - x[2]) * (gy - y[2]) - (y[0] - y[2]) * (gx - x[2])) / area
        w2 = 1.0 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        z = w0 * zs[0] + w1 * zs[1] + w2 * zs[2] + zbias
        zb = self.z[y0:y1 + 1, x0:x1 + 1]
        m = inside & (z < zb)
        zb[m] = z[m]
        self.col[y0:y1 + 1, x0:x1 + 1][m] = col
        self.tag[y0:y1 + 1, x0:x1 + 1][m] = tag

    def quad(self, xs, ys, zs, col, tag=0, zbias=0.0):
        self.tri(xs[:3], ys[:3], zs[:3], col, tag, zbias)
        self.tri([xs[0], xs[2], xs[3]], [ys[0], ys[2], ys[3]], [zs[0], zs[2], zs[3]], col, tag, zbias)

    def sprite(self, px, cx, cy, h, z, tag=0):
        """a square sprite of half size h (16.16) centred at cx, cy at depth z"""
        S = px.shape[0]
        x0, x1 = (cx - h) / 65536.0 - self.ox, (cx + h) / 65536.0 - self.ox
        y0, y1 = (cy - h) / 65536.0 - self.oy, (cy + h) / 65536.0 - self.oy
        for j in range(max(0, int(y0)), min(self.h, int(math.ceil(y1)))):
            v = int((j + 0.5 - y0) / (y1 - y0) * S)
            if not 0 <= v < S:
                continue
            for i in range(max(0, int(x0)), min(self.w, int(math.ceil(x1)))):
                u = int((i + 0.5 - x0) / (x1 - x0) * S)
                if 0 <= u < S and px[v, u] and z < self.z[j, i]:
                    self.z[j, i] = z
                    self.col[j, i] = px[v, u]
                    self.tag[j, i] = tag


def fog_col(col, lv):
    lv = max(0, min(5, lv))
    w = col[lv >> 1]
    return (w & 0xFFFF) if lv & 1 else (w >> 16)
