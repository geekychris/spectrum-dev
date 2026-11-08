"""Boing Twist's geometry for the Next port, from the original's render.c:
the camera (fovy 45, the eye at the origin looking down -z), the ball's
turn (yaw about its axis, then pitch, then roll), the room's box, and the
picture's layout on the ball (lon east from 0, lat from the north pole).

Everything the Next draws is looked up in tables made from these: for each
screen pixel of the ball, the texel of the picture it shows.
"""
import math

import numpy as np

W, H = 256, 192                       # the screen
HM = 96                               # Layer 2's rows: each shown twice (the copper)
FOVY = 45.0
FOCAL = (H * 0.5) / math.tan(math.radians(FOVY / 2))   # pixels per unit at distance 1
ROOM_Z = -6.5                         # back wall
ROOM_Y = -1.5                         # floor
BOX_X = 3.0                           # side walls
BOX_Y = 2.2                           # ceiling
NEAR_Z = 0.5

TEX_W, TEX_H = 256, 96                # the picture on the ball: lon x lat texels
TF_H = 64                             # the flip's texture: 0..90 degrees from the flip axis


def rot_axis(ax, ay, az, deg):
    a = math.radians(deg)
    c, s, t = math.cos(a), math.sin(a), 1 - math.cos(a)
    return np.array([
        [t * ax * ax + c, t * ax * ay - s * az, t * ax * az + s * ay],
        [t * ax * ay + s * az, t * ay * ay + c, t * ay * az - s * ax],
        [t * ax * az - s * ay, t * ay * az + s * ax, t * az * az + c],
    ])


def ball_rot(yaw, pitch, roll):
    """render.c ball_rot: yaw about the ball's axis, then the axis tipped and leaned"""
    return rot_axis(0, 0, 1, roll) @ rot_axis(1, 0, 0, pitch) @ rot_axis(0, 1, 0, yaw)


def project(x, y, z):
    return W * 0.5 + FOCAL * x / -z, H * 0.5 - FOCAL * y / -z


def ball_extent(vx, vy, dist):
    """render.c ball_extent: the ball's outline on screen (an ellipse in perspective)"""
    dxz, dyz = math.hypot(vx, dist), math.hypot(vy, dist)
    ax, ay = math.atan2(vx, dist), math.atan2(vy, dist)
    hx, hy = math.asin(1 / dxz), math.asin(1 / dyz)
    xl = W * 0.5 + FOCAL * math.tan(ax - hx)
    xr = W * 0.5 + FOCAL * math.tan(ax + hx)
    yt = H * 0.5 - FOCAL * math.tan(ay + hy)
    yb = H * 0.5 - FOCAL * math.tan(ay - hy)
    return xl, xr, yt, yb


def ball_pixels(vx, vy, dist, yaw, pitch, roll):
    """every pixel the ball covers: (xs, ys, q) with q the point of the ball
    (its own frame, unit vectors) the pixel shows (render.c rd_pick). ys are
    Layer 2's rows: row k is the screen's 2k and 2k + 1 (seen through the
    middle, y = 2k + 1)"""
    xl, xr, yt, yb = ball_extent(vx, vy, dist)
    x0, x1 = int(math.floor(xl)) - 1, int(math.ceil(xr)) + 1
    y0, y1 = int(math.floor(yt / 2)) - 1, int(math.ceil(yb / 2)) + 1
    ys, xs = np.mgrid[y0:y1 + 1, x0:x1 + 1]
    d = np.stack([(xs + 0.5 - W * 0.5) / FOCAL, -(2 * ys + 1.0 - H * 0.5) / FOCAL, -np.ones(xs.shape)], -1)
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    C = np.array([vx, vy, -dist])
    b = d @ C
    c = C @ C - 1.0
    disc = b * b - c
    hit = disc >= 0
    t = b - np.sqrt(np.where(hit, disc, 0))
    p = t[..., None] * d - C
    R = ball_rot(yaw, pitch, roll)
    q = p @ R                         # R^T p for each row vector
    return xs[hit], ys[hit], q[hit], (xl, xr, yt, yb)


def latlon(q):
    lat = np.degrees(np.arcsin(np.clip(q[:, 1], -1, 1)))
    lon = np.degrees(np.arctan2(q[:, 0], q[:, 2])) % 360.0
    return lat, lon


def tex_uv(lat, lon):
    u = np.floor(lon * TEX_W / 360.0).astype(int) % TEX_W
    v = np.clip(np.floor((90.0 - lat) * TEX_H / 180.0).astype(int), 0, TEX_H - 1)
    return u, v


def flip_frame(q):
    """a point's place round the flip axis: the axis A is the half's middle,
    (0, 0, 1) when the half faces the eye at yaw 0; alpha is the angle round
    A (from east towards north, so a turn about A by +phi adds phi), beta the
    angle from A"""
    alpha = np.degrees(np.arctan2(q[:, 1], q[:, 0])) % 360.0
    beta = np.degrees(np.arccos(np.clip(q[:, 2], -1, 1)))
    return alpha, beta


def tf_uv(alpha, beta):
    a = np.floor(alpha * TEX_W / 360.0).astype(int) % TEX_W
    b = np.clip(np.floor(beta * TF_H / 90.0).astype(int), 0, TF_H - 1)
    return a, b


def tile_cols(segs):
    """the texture's first column of each segment (and TEX_W at the end)"""
    return [int(round(s * TEX_W / segs)) for s in range(segs + 1)]


def tile_rows(bands):
    return [b * TEX_H // bands for b in range(bands + 1)]
