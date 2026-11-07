"""Marble Tilt's board geometry and camera, as the original's render.c and
the amiga3d engine compute them (integer for integer): the camera, the
board rolled and pitched about its middle, the projection. Shared by
gen_view.py (the tables the Next draws from) and its tests."""

SIN_TAB = None


def _sin_tab():
    global SIN_TAB
    if SIN_TAB is None:
        import os
        import re
        src = open(os.path.join(os.path.dirname(__file__), "orig", "amiga3d", "a3d_math.c")).read()
        body = src[src.index("a3_sin_tab[256] = {"):]
        body = body[body.index("{") + 1:body.index("};")]
        SIN_TAB = [int(v) for v in re.findall(r"-?\d+", body)]
        assert len(SIN_TAB) == 256
    return SIN_TAB


def tdiv(a, b):
    """C division (truncates toward zero)"""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b >= 0) else -q


def a3_sin(a):
    t = _sin_tab()
    i, f = (a >> 8) & 255, a & 255
    s0, s1 = t[i], t[(i + 1) & 255]
    return s0 + (((s1 - s0) * f) >> 8)


def a3_cos(a):
    return a3_sin(a + 16384)


def a3_angle(rad):
    v = (rad * 683565276) >> 32
    return ((v + 32768) & 0xFFFF) - 32768


MT_N, MT_CELL, MT_R = 12, 40, 9
HALF = MT_N * MT_CELL // 2
WALL_H = 22
GATE_OPEN_H = 4
BUMPER_H, BUMPER_R = 18, 13
TILT_MAX = 7864                      # FX(0.12)
TILTS = 5                            # tilt steps each way: the original eases in 5


class View:
    """the original's camera (render.c scene()) with the board tilted by
    roll and pitch (16.16 radians, drawn at twice the tilt)"""
    CAM_Y, CAM_Z, CAM_PITCH = 470, -430, 7600

    def __init__(self, roll, pitch, fov=266, cx=160, cy=134):
        ar, ap = a3_angle(roll * 2), a3_angle(pitch * 2)
        self.cr, self.sr = a3_cos(ar), a3_sin(ar)
        self.cp, self.sp = a3_cos(ap), a3_sin(ap)
        self.ccp, self.csp = a3_cos(self.CAM_PITCH), a3_sin(self.CAM_PITCH)
        self.fov, self.cx, self.cy = fov, cx, cy

    def cam(self, x, y, z):
        """bpt(): rolled, then pitched, then cam_xform (camera space)"""
        x1 = (x * self.cr + y * self.sr) >> 16
        y1 = (y * self.cr - x * self.sr) >> 16
        z2 = (z * self.cp + y1 * self.sp) >> 16
        y2 = (y1 * self.cp - z * self.sp) >> 16
        dx, dy, dz = x1, y2 - self.CAM_Y, z2 - self.CAM_Z
        rx = (dx * (65536 >> 4)) >> 12
        rz = (dz * (65536 >> 4)) >> 12
        oy = (dy * (self.ccp >> 4) + rz * (self.csp >> 4)) >> 12
        oz = (rz * (self.ccp >> 4) - dy * (self.csp >> 4)) >> 12
        return rx, oy, oz

    def project(self, x, y, z):
        """a board point -> the screen (cam_project)"""
        rx, ry, rz = self.cam(x, y, z)
        return self.cx + tdiv(rx * self.fov, rz), self.cy - tdiv(ry * self.fov, rz)


def tilt_value(k):
    """the tilt the original eases through, step k of -TILTS..TILTS"""
    return tdiv(k * TILT_MAX, TILTS)


def tilt_step(v):
    """the step drawn for a tilt value (nearest)"""
    return tdiv(v * TILTS + (TILT_MAX // 2 if v >= 0 else -(TILT_MAX // 2)), TILT_MAX)
