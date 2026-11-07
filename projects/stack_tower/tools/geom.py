"""Stack Tower's tower as the original's render.c builds it, on the host.

The camera, projection and faces are amiga3d's (a3d_math.c cam_xform /
cam_project) and render.c's (tower_pt, face, brick, the core, the grid, the
red line), with the original's numbers; face() keeps the original's
back-face test. gen_gfx.py draws these polygons into the pictures the Next
copies and blits. raster() fills a convex polygon at pixel centres, so
polygons that share an edge tile without gaps or overlaps.
"""
import math

import numpy as np

TOWER_R, BRICK, ROW_H = 140, 22, 26
ST_ROWS, ST_TOTAL, ST_COLS = 20, 24, 16
CAM_Y, CAM_Z, PITCH, FOV, CX, CY = 300, -640, 900, 200, 100, 120
VIEW_W, VIEW_H = 200, 256
SP, CP = math.sin(PITCH * 2 * math.pi / 65536), math.cos(PITCH * 2 * math.pi / 65536)

SHADE, LIT = 1, 2                      # brick image codes (0: transparent)


def cam(colf, r, y):
    """render.c tower_pt: column position colf (8.8, 0 = the column facing
    you), radius r, height y -> camera space"""
    a = colf * 16 * 2 * math.pi / 65536
    dx, dy, dz = math.sin(a) * r, y - CAM_Y, -math.cos(a) * r - CAM_Z
    return (dx, dy * CP + dz * SP, dz * CP - dy * SP)


def proj(p):
    return (CX + p[0] * FOV / p[2], CY - p[1] * FOV / p[2])


def faces_toward(a, b, c):
    ux, uy, uz = (b[i] - a[i] for i in range(3))
    vx, vy, vz = (c[i] - a[i] for i in range(3))
    nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
    return nx * a[0] + ny * a[1] + nz * a[2] < 0


def face(out, a, b, c, d, code):
    """render.c face(): a quad, only if it faces the camera"""
    if faces_toward(a, b, c):
        out.append(([proj(a), proj(b), proj(c), proj(d)], code))


def brick(colf, row, out_r):
    """render.c brick(): the faces of one block that show, in drawing order:
    the outside (a shaded rim round a lit centre), top, bottom, sides"""
    y0, y1 = row * ROW_H, row * ROW_H + ROW_H
    c0, c1, r0, r1 = colf - 128, colf + 128, TOWER_R, TOWER_R + out_r
    p = [cam(c0, r0, y0), cam(c1, r0, y0), cam(c1, r0, y1), cam(c0, r0, y1),
         cam(c0, r1, y0), cam(c1, r1, y0), cam(c1, r1, y1), cam(c0, r1, y1)]
    f = []
    face(f, p[4], p[7], p[6], p[5], SHADE)
    q = [cam(c0 + 44, r1 + 1, y0 + 5), cam(c1 - 44, r1 + 1, y0 + 5),
         cam(c1 - 44, r1 + 1, y1 - 5), cam(c0 + 44, r1 + 1, y1 - 5)]
    if faces_toward(q[0], q[3], q[2]):
        f.append(([proj(q[0]), proj(q[3]), proj(q[2]), proj(q[1])], LIT))
    face(f, p[7], p[3], p[2], p[6], LIT)       # top
    face(f, p[4], p[5], p[1], p[0], SHADE)     # bottom
    face(f, p[0], p[3], p[7], p[4], SHADE)     # left side
    face(f, p[1], p[5], p[6], p[2], SHADE)     # right side
    return f


def core_edge_y(colf, row):
    """the screen row of the core's (TOWER_R - 3) edge between rows row - 1
    and row, in the middle of the column at colf"""
    return proj(cam(colf, TOWER_R - 3, row * ROW_H))[1]


def grid_quads(colf, edges, rows):
    """grid lines (render.c): column edges +-6 wide, row edges +-1 high, at TOWER_R - 1"""
    r, f = TOWER_R - 1, []
    for ce in edges:
        face(f, cam(ce - 6, r, 0), cam(ce - 6, r, ST_ROWS * ROW_H), cam(ce + 6, r, ST_ROWS * ROW_H),
             cam(ce + 6, r, 0), 0)
    for rr in rows:
        if rr > ST_ROWS:
            continue
        a, b = cam(colf - 128, r, rr * ROW_H - 1), cam(colf + 128, r, rr * ROW_H - 1)
        c, d = cam(colf + 128, r, rr * ROW_H + 1), cam(colf - 128, r, rr * ROW_H + 1)
        face(f, a, d, c, b, 0)
    return f


def core_faces(frac):
    """the core, its grid and the red line for a view whose fraction of a
    column is frac (0..255): (polygon, kind) in drawing order; the core's
    panels are ("core", k): panel k is the column k to the right of the one
    facing you (k 0..15, round the back)"""
    R = TOWER_R
    out = []
    for k in range(16):
        c0 = (k << 8) - 128 - frac
        face(out, cam(c0, R - 3, -20), cam(c0, R - 3, ST_ROWS * ROW_H + 30),
             cam(c0 + 256, R - 3, ST_ROWS * ROW_H + 30), cam(c0 + 256, R - 3, -20), ("core", k))
    for k in range(-5, 7):
        ce = (k << 8) - 128 - frac
        for poly, _ in grid_quads(0, [ce], []):
            out.append((poly, "grid"))
    for row in range(ST_ROWS + 1):
        for k in range(-5, 6):
            c0 = (k << 8) - 128 - frac
            for poly, _ in grid_quads(c0 + 128, [], [row]):
                out.append((poly, "grid"))
    for k in range(-8, 8):
        c0 = k * 256 - frac
        face(out, cam(c0, R + 2, ST_ROWS * ROW_H - 2), cam(c0, R + 2, ST_ROWS * ROW_H + 2),
             cam(c0 + 256, R + 2, ST_ROWS * ROW_H + 2), cam(c0 + 256, R + 2, ST_ROWS * ROW_H - 2), "line")
    return out


def raster(img, poly, val):
    """fill a convex polygon into a numpy image (rows, columns): the pixels
    whose centres are inside"""
    h, w = img.shape
    ys = [p[1] for p in poly]
    n = len(poly)
    for y in range(max(0, math.ceil(min(ys) - 0.5)), min(h - 1, math.ceil(max(ys) - 0.5) - 1) + 1):
        yc = y + 0.5
        xs = []
        for i in range(n):
            (xa, ya), (xb, yb) = poly[i], poly[(i + 1) % n]
            if (ya <= yc < yb) or (yb <= yc < ya):
                xs.append(xa + (yc - ya) * (xb - xa) / (yb - ya))
        if len(xs) < 2:
            continue
        x0, x1 = max(0, math.ceil(min(xs) - 0.5)), min(w - 1, math.ceil(max(xs) - 0.5) - 1)
        if x1 >= x0:
            img[y, x0:x1 + 1] = val


def render(faces, w=VIEW_W, h=VIEW_H):
    img = np.zeros((h, w), np.uint8)
    for poly, code in faces:
        raster(img, poly, code)
    return img
