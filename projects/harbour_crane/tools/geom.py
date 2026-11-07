"""The amiga3d engine's camera as Harbour Crane uses it (tools/orig/a3d_math.c
cam_xform, cam_project; tools/orig/render.c scene()), in the same integer
arithmetic, for the Next's 256 x 192 view.

port: the original's camera follows the trolley a little
(cam.x = 60 + (tx - 100) / 6, 7..115); the Next's is fixed at its middle
(cam.x = 60). Its view is the original's 320 x 256 at 0.8: the original's
rows 12..252 (between its top and bottom panels) are the Next's 0..191.
src/view.c projects with the same numbers.
"""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))

CAM_X, CAM_Y, CAM_Z = 60, 150, -760
PITCH = 1100                       # the engine's 16-bit angle
VIEW_W, VIEW_H = 256, 192
VIEW_CX = 128                      # (160 * 0.8)
VIEW_CY = 99                       # ((136 - 12) * 0.8)
FOV = 232                          # (290 * 0.8)

_tab = [int(v) for v in re.search(r"a3_sin_tab\[256\] = \{(.*?)\};",
                                  open(os.path.join(HERE, "orig", "a3d_math.c")).read(), re.S)
        .group(1).replace("\n", "").split(",") if v.strip()]


def a3_sin(a):
    i, f = (a >> 8) & 255, a & 255
    s0, s1 = _tab[i], _tab[(i + 1) & 255]
    return s0 + (((s1 - s0) * f) >> 8)


def a3_cos(a):
    return a3_sin(a + 16384)


CP12 = a3_cos(PITCH) >> 4          # Q12, as cam_xform uses them
SP12 = a3_sin(PITCH) >> 4


def xform(x, y, z):
    """world -> camera space (cam_xform with yaw 0)"""
    dx, dy, dz = x - CAM_X, y - CAM_Y, z - CAM_Z
    return dx, (dy * CP12 + dz * SP12) >> 12, (dz * CP12 - dy * SP12) >> 12


def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def project(v):
    x, y, z = v
    return VIEW_CX + cdiv(x * FOV, z), VIEW_CY - cdiv(y * FOV, z)


def unproject_water(sx, sy, wy):
    """the point on the plane y = wy seen at screen pixel centre (sx, sy), or None"""
    import math
    cp, sp = CP12 / 4096.0, SP12 / 4096.0
    # camera-space direction of the pixel
    dxc = (sx + 0.5 - VIEW_CX) / FOV
    dyc = (VIEW_CY - (sy + 0.5)) / FOV
    # camera space (x, y', z') with z' = 1: back to world deltas (inverse of the pitch rotation)
    dy = dyc * cp - 1.0 * sp
    dz = dyc * sp + 1.0 * cp
    if dy >= 0:
        return None
    t = (wy - CAM_Y) / dy
    return CAM_X + dxc * t, CAM_Z + dz * t
