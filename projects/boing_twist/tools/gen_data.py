#!/usr/bin/env python3
"""Boing Twist's data for the Next, made from the original's own geometry
(tools/geom.py is render.c's camera, ball and room) and pictures.

The ball is drawn by table: for every screen pixel of the ball, a record
(u, v, shade) says which texel of the picture it shows when the ball is at
yaw 0 (u: longitude, 256 round the ball; v: latitude, 96 pole to pole) and
how lit it is there (the original's light, fixed to the eye: its shading
table, in 8 levels, dithered). Turning the ball is adding to u; a twist is
the band's rows of the texture turned; a flip is drawn from a second
texture laid out round the flip's axis (TF: alpha round it, beta from it),
where the flip is again an add.

  views     the play view at each tilt (port: -36..36 degrees in steps of 12,
            the original's W / S steps; it allows -60..60), each with its
            flip view (the pixels the flipping half covers, as TF texels,
            and any the other half shows, as picture texels); the title's
            leaning ball (roll -17, further away); the select screen's ball
  ct        TF from the picture: for each TF texel, the picture texel
            (u relative to the half's middle, v): drawn by the same code
  pictures  256 x 96 texels in 25 colours (+ groove, ring, half line);
            per picture: the 224 palette entries (28 colours x 8 shades)
            and which tiles look alike (bt_set_sig) for each grid
  rooms     the original's ten rooms drawn from its box (ray cast, its
            textures and grids), 4 bits a pixel (16 colours and their
            shadowed copies in the palette), at half height like the ball

port: Layer 2 is drawn 96 rows high and the copper shows each row twice
(src/main.c), which halves the ball's pixels; everything here is made for
those rows.

Output: gfx/bank_NN.bin, src/gfx_data.asm, src/gfx_data.h, src/gfx_data.c.
    python3 tools/gen_data.py [--preview DIR]
"""
import argparse
import math
import os
import re
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.dirname(HERE)
ORIG = os.path.normpath(os.path.join(PROJ, "..", "..", "..", "amiga_mcp", "examples", "boing_twist"))
sys.path.insert(0, HERE)
import geom  # noqa: E402
from geom import FOCAL, TEX_W, TEX_H, TF_H, W, H, HM  # noqa: E402

FIRST_BANK = 30                      # banks below: Layer 2, the working banks and code (src/bt.h)
PAGE = 8192
PITCHES = [-36, -24, -12, 0, 12, 24, 36]
PLAY_DIST = 3.6
TITLE = dict(vx=0.0, vy=0.0, dist=4.4, pitch=0.0, roll=-17.0)
SELECT = dict(vx=1.05, vy=0.05, dist=3.9, pitch=12.0, roll=0.0)
NCOL = 25                            # picture colours; 25 groove, 26 ring (yellow), 27 half (cyan)
C_GROOVE, C_RING, C_HALF = 25, 26, 27
GROOVE_RGB = (0x3A, 0x3C, 0x48)
RING_RGB = (255, 220, 40)
HALF_RGB = (40, 230, 255)
GRIDS = [(4, 8), (6, 12), (8, 16)]   # bt_levels' grids (Expert is Hard's)
LEVELS = {"easy": 0, "medium": 1, "hard": 2, "expert": 3}

# the shading levels: (multiplier, highlight) as the original's table
# entries (m capped at 1, highlight added); a pixel is dithered between the
# two levels around its own
SHADES = [(0.30, 0), (0.42, 0), (0.55, 0), (0.68, 0), (0.81, 0), (0.94, 0), (1.0, 28), (1.0, 100)]
BAYER = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 + 1 / 32.0


def rgb9(r, g, b):
    """an RGB888 colour -> the Next's 9-bit RRRGGGBBB, never the transparent 0xE3"""
    r3, g3, b3 = (int(round(min(255, max(0, c)) * 7 / 255)) for c in (r, g, b))
    v = r3 << 6 | g3 << 3 | b3
    if v >> 1 == 0xE3:               # Layer 2's global transparency (RGB332 0xE3)
        v ^= 1 << 3                  # one step of green
    return v


def shade_metric(m, a):
    return min(m, 1.0) * 0.5 + a / 255.0


SH_T = [shade_metric(m, a) for m, a in SHADES]


def shading(xs, ys, ext):
    """render.c build_shade + shade_ball: the light fixed to the eye, a
    pixel's shade from where it is in the ball's outline; -> level 0..7"""
    xl, xr, yt, yb = ext
    cx, cy = (xl + xr) * 0.5, (yt + yb) * 0.5
    x = (xs + 0.5 - cx) / ((xr - xl) * 0.5)
    y = -(2 * ys + 1.0 - cy) / ((yb - yt) * 0.5)        # (Layer 2 row k: the screen's 2k + 1)
    rr = x * x + y * y
    k = np.where(rr > 1, 1 / np.sqrt(np.maximum(rr, 1e-9)), 1.0)
    x, y = x * k, y * k
    rr = np.minimum(rr, 1.0)
    z = np.sqrt(1 - rr)
    L = (-0.42, 0.52, 0.74)
    h = np.array([L[0], L[1], L[2] + 1.0])
    h /= np.linalg.norm(h)
    d = x * L[0] + y * L[1] + z * L[2]
    m = 0.30 + 0.80 * np.maximum(d, 0)
    s = np.maximum(x * h[0] + y * h[1] + z * h[2], 0) ** 32
    t = np.minimum(m, 1.0) * 0.5 + s * 150 / 255.0
    thr = BAYER[ys % 4, xs % 4]
    lvl = np.zeros(len(t), dtype=int)
    for i in range(len(SH_T) - 1):
        lo, hi = SH_T[i], SH_T[i + 1]
        f = (t - lo) / (hi - lo)
        lvl = np.where(t >= hi, i + 1, np.where((t >= lo) & (f > thr), i + 1, lvl))
    return lvl


# ---------------------------------------------------------------- streams
class Stream:
    """rows of (dy, dx0, n) + n records of 3 bytes, packed into 8K pages
    (a row never crosses a page: 0x80 = go on at the next page, 0x7F = end)"""

    def __init__(self, cx, cy):
        self.cx, self.cy = cx, cy
        self.pages = [bytearray()]
        self.pixels = 0
        self.rows = 0

    def row(self, dy, dx0, recs):
        assert -127 <= dy <= 126 and -128 <= dx0 <= 127 and 0 < len(recs) <= 255
        blob = bytes([dy & 255, dx0 & 255, len(recs)]) + bytes(b for r in recs for b in r)
        if len(self.pages[-1]) + len(blob) + 1 > PAGE:
            self.pages[-1].append(0x80)
            self.pages.append(bytearray())
        self.pages[-1] += blob
        self.pixels += len(recs)
        self.rows += 1

    def end(self):
        self.pages[-1].append(0x7F)
        return self


def pixels_to_stream(xs, ys, recs, cx, cy):
    """recs[i] = (a, b, c) for pixel (xs[i], ys[i]); rows top to bottom, each
    a run left to right (the ball is convex: one run a row)"""
    st = Stream(cx, cy)
    order = np.lexsort((xs, ys))
    xs, ys = xs[order], ys[order]
    recs = [recs[i] for i in order]
    i = 0
    while i < len(xs):
        j = i
        while j + 1 < len(xs) and ys[j + 1] == ys[i]:
            j += 1
        assert xs[j] - xs[i] == j - i, "a row of the ball with a hole in it"
        st.row(int(ys[i] - cy), int(xs[i] - cx), recs[i:j + 1])
        i = j + 1
    return st.end()


def view(vx, vy, dist, pitch, roll):
    xs, ys, q, ext = geom.ball_pixels(vx, vy, dist, 0.0, pitch, roll)
    lat, lon = geom.latlon(q)
    u, v = geom.tex_uv(lat, lon)
    lvl = shading(xs, ys, ext)
    cx, cy = geom.project(vx, vy, -dist)
    cx, cy = int(round(cx)), int(round((cy - 1) / 2))   # in Layer 2's rows
    return xs, ys, q, u, v, lvl, cx, cy


def ball_stream(vx, vy, dist, pitch, roll):
    xs, ys, q, u, v, lvl, cx, cy = view(vx, vy, dist, pitch, roll)
    recs = [(int(u[i]), int(v[i]), int(lvl[i]) << 5) for i in range(len(xs))]
    return pixels_to_stream(xs, ys, recs, cx, cy), (xs, ys, u, v, lvl)


def flip_streams(pitch):
    """the play view at this tilt with the half facing the eye: the pixels on
    the flipping half as TF texels, the rest (only at steep tilts) as the
    picture's"""
    xs, ys, q, u, v, lvl, cx, cy = view(0.0, 0.0, PLAY_DIST, pitch, 0.0)
    alpha, beta = geom.flip_frame(q)
    a, b = geom.tf_uv(alpha, beta)
    on = beta < 90.0
    main = pixels_to_stream(xs[on], ys[on], [(int(a[i]), int(b[i]), int(lvl[i]) << 5) for i in np.nonzero(on)[0]],
                            cx, cy)
    off = ~on
    if off.any():
        # the far half shows at the rim: several runs a row there, so a stream of single pixels
        st = Stream(cx, cy)
        idx = np.nonzero(off)[0]
        idx = idx[np.lexsort((xs[idx], ys[idx]))]
        k = 0
        while k < len(idx):
            m = k
            while m + 1 < len(idx) and ys[idx[m + 1]] == ys[idx[k]] and xs[idx[m + 1]] == xs[idx[m]] + 1:
                m += 1
            st.row(int(ys[idx[k]] - cy), int(xs[idx[k]] - cx),
                   [(int(u[i]), int(v[i]), int(lvl[i]) << 5) for i in idx[k:m + 1]])
            k = m + 1
        static = st.end()
    else:
        static = None
    return main, static


def ct_stream():
    """TF from the picture: TF texel (alpha, beta) is the picture's at the
    point beta from the flip axis, alpha round it; u relative to the half's
    middle (the renderer adds the half's longitude), shade 0"""
    st = Stream(128, 0)
    for r in range(TF_H):
        recs = [ct_uv(c * 360.0 / TEX_W, (r + 0.5) * 90.0 / TF_H) + (0,) for c in range(TEX_W)]
        st.row(r, -128, recs[:128])
        st.row(r, 0, recs[128:])
    return st.end()


def ct_uv(alpha_deg, beta_deg):
    """the picture texel (u relative to the half's middle, v) at alpha, beta round the flip axis"""
    beta, al = math.radians(beta_deg), math.radians(alpha_deg)
    px, py, pz = math.sin(beta) * math.cos(al), math.sin(beta) * math.sin(al), math.cos(beta)
    lat = math.degrees(math.asin(max(-1.0, min(1.0, py))))
    lon = math.degrees(math.atan2(px, pz)) % 360.0
    return (int(math.floor(lon * TEX_W / 360.0)) % TEX_W,
            min(TEX_H - 1, max(0, int(math.floor((90.0 - lat) * TEX_H / 180.0)))))


def ring_texels():
    """the ring round the flip axis with its two arrow heads (render.c: radius
    0.42 on the ball, arcs 20..160 and 200..340 degrees, heads at 160 and 340
    reaching back to 142 at radii 0.35 and 0.49), as picture texels (u
    relative to the half's middle, v): drawn into the texture"""
    pts = []

    def put(r, t):
        uv = ct_uv(t % 360.0, math.degrees(math.asin(r)))
        if uv not in pts:
            pts.append(uv)

    for k in range(2):
        for j in range(0, 141):
            put(0.42, k * 180 + 20 + j)
        for rq in (0.35, 0.49):
            for j in range(13):
                f = j / 12.0
                put(0.42 + (rq - 0.42) * f, k * 180 + 160 + (142 - 160) * f)
    return pts


# ---------------------------------------------------------------- pictures
def read_titles():
    pics = []
    for line in open(os.path.join(ORIG, "images", "titles.txt")):
        line = line.split("#")[0].strip()
        if not line or "=" not in line:
            continue
        name, rest = (s.strip() for s in line.split("=", 1))
        parts = [p.strip() for p in rest.split("|")]
        title = parts[0].upper()[:24]
        level = LEVELS[parts[1].lower()] if len(parts) > 1 and parts[1] else 0
        room = parts[2] if len(parts) > 2 else ""
        pics.append((int(name.split("_")[0]), name, title, level, room))
    pics.sort()
    return pics


def quantize_picture(path):
    im = Image.open(path).convert("RGB").resize((TEX_W, TEX_H), Image.LANCZOS)
    q = im.quantize(colors=NCOL, method=Image.Quantize.MEDIANCUT, kmeans=4, dither=Image.Dither.NONE)
    pal = q.getpalette()[:NCOL * 3]
    idx = np.array(q, dtype=np.uint8)
    cols = [tuple(pal[3 * i:3 * i + 3]) for i in range(NCOL)]
    return idx, cols


def picture_palette(cols):
    """the 224 entries: shade level k, colour c at k * 32 + c (c < 28)"""
    allc = list(cols) + [(0, 0, 0)] * (NCOL - len(cols)) + [GROOVE_RGB, RING_RGB, HALF_RGB]
    out = []
    for k, (m, a) in enumerate(SHADES):
        e0 = 255 if m > 1 else m * 255
        for c in allc:
            out.append(rgb9(*((ch * e0 / 256.0) + a for ch in c)))
    return out


def sig_classes(idx, bands, segs):
    """which tiles look alike (render.c sig_of, on the Next's texels): for home
    tile h, upright and turned, a class number; alike tiles share one"""
    cols, rows = geom.tile_cols(segs), geom.tile_rows(bands)
    seen, out = {}, []
    for h in range(bands * segs):
        hb, hs = divmod(h, segs)
        t = idx[rows[hb]:rows[hb + 1], cols[hs]:cols[hs + 1]]
        for turned in (0, 1):
            key = (t[::-1, ::-1] if turned else t).tobytes() + bytes(t.shape)
            out.append(seen.setdefault(key, len(seen)))
    assert len(seen) <= 256
    return out + [0] * (256 - len(out))


# ---------------------------------------------------------------- rooms
def read_theme(path):
    th = {"faces": {}}
    for line in open(path):
        line = line.split("#")[0].strip()
        if "=" not in line:
            continue
        k, v = (s.strip() for s in line.split("=", 1))
        if k == "name":
            th["name"] = v.upper()
        elif k == "background":
            th["bg"] = int(v, 16)
        elif k == "shadow":
            p = v.split()
            th["shadow"] = (p[0], int(p[1])) if p[0] != "off" else ("off", 0)
        elif k in ("back", "floor", "left", "right", "ceiling"):
            p = v.split()
            if p[0] == "off":
                continue
            f = {"col": int(p[0], 16), "grid": None if p[1].lower() == "none" else int(p[1], 16),
                 "cell": float(p[2]), "tex": None, "repeat": 0}
            if len(p) > 3:
                f["tex"] = p[3]
            if len(p) > 4:
                f["repeat"] = float(p[4])
            th["faces"][k] = f
    return th


def theme_texture(name):
    base = os.path.splitext(name)[0]
    src = os.path.join(ORIG, "themes", "src", base + ".jpg")
    if os.path.exists(src):
        return np.asarray(Image.open(src).convert("RGB"), dtype=np.float32)
    if base == "stars":
        rng = np.random.RandomState(7)              # make_texture.py --stars: a star field
        img = np.zeros((512, 1024, 3), np.float32)
        for _ in range(900):
            x, y, b = rng.randint(1024), rng.randint(512), rng.uniform(80, 255)
            img[y, x] = (b, b, min(255, b * 1.1))
        return img
    raise SystemExit(f"no texture {name}")


def render_room(th, ss=4):
    """render.c rd_set_theme, by ray casting: the box's faces (the back wall's
    and floor's textures stretched over their visible parts, a repeat tiles
    them), grids as lines about a pixel wide, at ss x ss samples a pixel"""
    WW, HH = W * ss, H * ss
    f = FOCAL * ss
    faces = th["faces"]
    xl = -geom.BOX_X if "left" in faces else -20.0
    xr = geom.BOX_X if "right" in faces else 20.0
    yt = geom.BOX_Y if "ceiling" in faces else 20.0
    zn = -geom.NEAR_Z * 1.01
    th_ = math.tan(math.radians(geom.FOVY / 2))
    hx, hy = -geom.ROOM_Z * th_ * W / H, -geom.ROOM_Z * th_
    bx0, bx1, by1 = max(xl, -hx), min(xr, hx), min(yt, hy)
    fz = geom.ROOM_Y / th_
    ys, xs = np.mgrid[0:HH, 0:WW]
    dx = (xs + 0.5 - WW * 0.5) / f
    dy = -(ys + 0.5 - HH * 0.5) / f
    bgc = np.array([(th["bg"] >> 16) & 255, (th["bg"] >> 8) & 255, th["bg"] & 255], np.float32)
    img = np.broadcast_to(bgc, (HH, WW, 3)).copy()
    best = np.full((HH, WW), np.inf)
    pix_w = 1.0 / f                    # one sample's width at distance 1

    def colour_of(c):
        return np.array([(c >> 16) & 255, (c >> 8) & 255, c & 255], np.float32)

    def shade_face(key, t, P, a_axis, b_axis, quad):
        """t: ray parameter (z distance), P: hit points; quad: corners c0..c3 for the texture"""
        fc = faces[key]
        mask = np.isfinite(t) & (t < best)
        if not mask.any():
            return
        col = np.broadcast_to(colour_of(fc["col"]), P.shape).copy()
        if fc["tex"]:
            tex = theme_texture(fc["tex"])
            c0, c1, c3 = (np.array(quad[i]) for i in (0, 1, 3))
            e1, e3 = c1 - c0, c3 - c0
            s = ((P - c0) @ e1) / (e1 @ e1)
            tt = ((P - c0) @ e3) / (e3 @ e3)
            ru = rv = 1.0
            if fc["repeat"] > 1:
                ru = fc["repeat"]
                rv = fc["repeat"] * math.sqrt((e3 @ e3) / (e1 @ e1))
            if fc["repeat"] > 1:
                uu, vv = (s * ru) % 1.0, ((1.0 - tt) * rv) % 1.0
            else:
                uu, vv = np.clip(s, 0, 0.9999), np.clip(1.0 - tt, 0, 0.9999)
            th_h, th_w = tex.shape[:2]
            col = tex[(vv * th_h).astype(int) % th_h, (uu * th_w).astype(int) % th_w]
        if fc["grid"] is not None and fc["cell"] > 0.05:
            g = colour_of(fc["grid"])
            on = np.zeros(t.shape, bool)
            for ax in (a_axis, b_axis):
                v = P[..., ax] / fc["cell"]
                dist = np.abs(v - np.round(v)) * fc["cell"]
                on |= dist < 0.55 * pix_w * t * ss   # about a pixel of the 256 x 192 screen
            col = np.where(on[..., None], g, col)
        img[mask] = col[mask]
        best[mask] = t[mask]

    def plane(axis, value):
        d = [dx, dy, -np.ones_like(dx)][axis]
        with np.errstate(divide="ignore", invalid="ignore"):
            t = value / d                       # the hit is t * (dx, dy, -1)
        t = np.where(t > 0, t, np.inf)
        P = np.stack([dx * t, dy * t, -t], -1)
        return t, P

    def within(P, ax, lo, hi):
        return (P[..., ax] >= lo - 1e-6) & (P[..., ax] <= hi + 1e-6)

    # z distance t = -z
    if "left" in faces:
        t, P = plane(0, xl)
        ok = within(P, 1, geom.ROOM_Y, yt) & within(P, 2, geom.ROOM_Z, zn)
        shade_face("left", np.where(ok, t, np.inf), P, 1, 2,
                   [(xl, geom.ROOM_Y, zn), (xl, geom.ROOM_Y, geom.ROOM_Z), (xl, yt, geom.ROOM_Z), (xl, yt, zn)])
    if "right" in faces:
        t, P = plane(0, xr)
        ok = within(P, 1, geom.ROOM_Y, yt) & within(P, 2, geom.ROOM_Z, zn)
        shade_face("right", np.where(ok, t, np.inf), P, 1, 2,
                   [(xr, geom.ROOM_Y, geom.ROOM_Z), (xr, geom.ROOM_Y, zn), (xr, yt, zn), (xr, yt, geom.ROOM_Z)])
    if "ceiling" in faces:
        t, P = plane(1, yt)
        ok = within(P, 0, xl, xr) & within(P, 2, geom.ROOM_Z, zn)
        shade_face("ceiling", np.where(ok, t, np.inf), P, 0, 2,
                   [(xl, yt, geom.ROOM_Z), (xr, yt, geom.ROOM_Z), (xr, yt, zn), (xl, yt, zn)])
    if "floor" in faces:
        t, P = plane(1, geom.ROOM_Y)
        ok = within(P, 0, xl, xr) & within(P, 2, geom.ROOM_Z, zn)
        if faces["floor"]["tex"]:
            quad = [(bx0, geom.ROOM_Y, fz), (bx1, geom.ROOM_Y, fz), (bx1, geom.ROOM_Y, geom.ROOM_Z),
                    (bx0, geom.ROOM_Y, geom.ROOM_Z)]
        else:
            quad = None
        shade_face("floor", np.where(ok, t, np.inf), P, 0, 2, quad)
    if "back" in faces:
        t, P = plane(2, geom.ROOM_Z)
        ok = within(P, 0, xl, xr) & within(P, 1, geom.ROOM_Y, yt)
        shade_face("back", np.where(ok, t, np.inf), P, 0, 1,
                   [(bx0, geom.ROOM_Y, geom.ROOM_Z), (bx1, geom.ROOM_Y, geom.ROOM_Z), (bx1, by1, geom.ROOM_Z),
                    (bx0, by1, geom.ROOM_Z)])
    img = img.reshape(HM, 2 * ss, W, ss, 3).mean(axis=(1, 3))      # Layer 2's rows: two screen rows each
    return np.clip(img, 0, 255).astype(np.uint8)


def room_data(th, img):
    """16 colours, 4 bits a pixel (two pixels a byte, the left one high), and
    the 32 palette entries: colour i at block (i >> 2) * 2, entry 28 + (i & 3),
    its shadowed copy in the next block (index | 0x20)"""
    im = Image.fromarray(img).quantize(colors=16, method=Image.Quantize.MEDIANCUT, kmeans=4,
                                      dither=Image.Dither.NONE)
    pal = im.getpalette()[:48]
    idx = np.array(im, dtype=np.uint8)
    packed = (idx[:, 0::2] << 4 | idx[:, 1::2]).astype(np.uint8).tobytes()
    kind, pct = th.get("shadow", ("wall", 45))
    k = (100 - pct) / 100.0 if kind != "off" else 1.0
    entries = []
    for i in range(16):
        c = pal[3 * i:3 * i + 3] if 3 * i + 2 < len(pal) else [0, 0, 0]
        entries.append(rgb9(*c))
        entries.append(rgb9(*(ch * k for ch in c)))
    return packed, entries, idx, pal


def room_index(i):
    return ((i >> 2) * 2) << 5 | (28 + (i & 3))


# ---------------------------------------------------------------- motion
def bounce_tables():
    """the title's and the solved screen's bounce (main_os4.c scene): per 50 Hz
    tick of a period, the ball's height (y = -0.5 + k |sin|) and sway (x), as
    screen offsets at the title's distance; and the ticks where it lands"""
    out = {}
    for name, (fb, kb, fx, ax) in {"title": (2.4, 1.7, 0.55, 1.6), "solved": (2.6, 1.5, 0.8, 1.0)}.items():
        nb = int(round(math.pi / fb * 50))
        nx = int(round(2 * math.pi / fx * 50))
        by = [int(round(-FOCAL * (-0.5 + kb * abs(math.sin(math.pi * i / nb))) / TITLE["dist"] / 2)) for i in range(nb)]
        bx = [int(round(FOCAL * ax * math.sin(2 * math.pi * i / nx) / TITLE["dist"])) for i in range(nx)]
        by_wall = [-0.5 + kb * abs(math.sin(math.pi * i / nb)) for i in range(nb)]
        out[name] = (by, bx, by_wall, [ax * math.sin(2 * math.pi * i / nx) for i in range(nx)])
    return out


def shadow_defines():
    out = []
    r = FOCAL * 1.15 / -geom.ROOM_Z
    _, floor_y = geom.project(0, geom.ROOM_Y, geom.ROOM_Z)
    out.append(f"#define SH_WALL_R {int(round(r))}          /* across; down: half (Layer 2's rows) */")
    out.append(f"#define SH_WALL_CLIP {int(floor_y) // 2}      /* rows below this are floor */")
    for name, (vx, vy, dist) in {"PLAY": (0.0, 0.0, PLAY_DIST), "SELECT": (SELECT["vx"], SELECT["vy"], SELECT["dist"]),
                                 "TITLE": (0.0, 0.0, TITLE["dist"])}.items():
        wx, wy = geom.project(vx + 0.85, vy - 0.15, geom.ROOM_Z)
        cx, cy = geom.project(vx, geom.ROOM_Y, -dist)
        _, sy0 = geom.project(vx, geom.ROOM_Y, -dist - 1.0)
        _, sy1 = geom.project(vx, geom.ROOM_Y, -dist + 0.6)
        out += [f"#define SH_{name}_WX {int(round(wx))}", f"#define SH_{name}_WY {int(round(wy / 2))}",
                f"#define SH_{name}_FX {int(round(cx))}", f"#define SH_{name}_FY {int(round((sy0 + sy1) / 4))}",
                f"#define SH_{name}_RX {int(round(FOCAL / dist))}", f"#define SH_{name}_RY {int(round((sy1 - sy0) / 4))}"]
    return out


# ---------------------------------------------------------------- output
class Pages:
    def __init__(self, first_page):
        self.first = first_page
        self.pages = []

    def add(self, blobs):
        """page-aligned blobs (each <= 8K) -> first page number"""
        p = self.first + len(self.pages)
        for b in blobs:
            assert len(b) <= PAGE
            self.pages.append(bytes(b) + bytes(PAGE - len(b)))
        return p

    def add_data(self, data):
        return self.add([data[i:i + PAGE] for i in range(0, len(data), PAGE)])


def c_str(s):
    return '"' + s.replace('"', "'") + '"'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="write preview PNGs here")
    args = ap.parse_args()
    pages = Pages(FIRST_BANK * 2)
    hdr, csrc = [], []

    # ---- views
    views = {}
    for p in PITCHES:
        st, _ = ball_stream(0.0, 0.0, PLAY_DIST, float(p), 0.0)
        fm, fs = flip_streams(float(p))
        views[f"play{p}"] = st
        views[f"flip{p}"] = fm
        if fs:
            views[f"fstat{p}"] = fs
    views["title"], _ = ball_stream(TITLE["vx"], TITLE["vy"], TITLE["dist"], TITLE["pitch"], TITLE["roll"])
    views["select"], _ = ball_stream(SELECT["vx"], SELECT["vy"], SELECT["dist"], SELECT["pitch"], SELECT["roll"])
    views["ct"] = ct_stream()
    vrec = {}
    for name, st in views.items():
        vrec[name] = (pages.add(st.pages), st.cx, st.cy, st.pixels, len(st.pages))
        print(f"  view {name:8s}: {st.pixels:6d} pixels, {st.rows:3d} rows, {len(st.pages)} pages")

    # ---- pictures
    pics = read_titles()
    meta = bytearray()
    pic_recs = []
    # the Boing ball (main_os4.c boing_picture: made by the game for each grid): its palette
    boing_pal = picture_palette([(0xE0, 0x18, 0x18), (0xF4, 0xF4, 0xF4)])
    meta += b"".join(v.to_bytes(2, "little") for v in boing_pal) + bytes(3 * 256)
    previews = []
    for num, name, title, level, room in pics:
        idx, cols = quantize_picture(os.path.join(ORIG, "images", name + ".jpg"))
        page = pages.add_data(idx.tobytes())
        pal = picture_palette(cols)
        sig = []
        for b, s in GRIDS:
            sig += sig_classes(idx, b, s)
        blob = b"".join(v.to_bytes(2, "little") for v in pal) + bytes(sig)
        pic_recs.append((title, level, room, page, len(meta)))
        meta += blob
        previews.append((name, idx, cols))
    # ---- rooms (the Boing room first, as the original)
    order = ["boing", "midnight", "space", "aquarium", "arcade", "garden", "observatory", "retroroom", "study",
             "workshop"]
    room_recs = []
    room_imgs = []
    for rname in order:
        th = read_theme(os.path.join(ORIG, "themes", rname + ".theme"))
        img = render_room(th)
        packed, entries, ridx, rpal = room_data(th, img)
        page = pages.add_data(packed)
        kind, pct = th.get("shadow", ("wall", 45))
        room_recs.append((rname, th["name"][:20], page, len(meta), 1 if kind == "floor" else 0 if kind == "wall" else 2))
        meta += b"".join(v.to_bytes(2, "little") for v in entries)
        room_imgs.append((rname, ridx, rpal))
        print(f"  room {th['name']}: shadow {kind} {pct}")
    meta_page = pages.add_data(bytes(meta))
    meta_pages = (len(meta) + PAGE - 1) // PAGE
    ring = ring_texels()

    # ---- banks out
    os.makedirs(os.path.join(PROJ, "gfx"), exist_ok=True)
    for f in os.listdir(os.path.join(PROJ, "gfx")):
        if re.match(r"bank_\d+\.bin$", f):
            os.remove(os.path.join(PROJ, "gfx", f))
    if len(pages.pages) % 2:
        pages.pages.append(bytes(PAGE))
    nbanks = len(pages.pages) // 2
    asm = ["; made by tools/gen_data.py - do not edit: the views, pictures and rooms, straight into banks", ""]
    for i in range(nbanks):
        bank = FIRST_BANK + i
        with open(os.path.join(PROJ, "gfx", f"bank_{bank}.bin"), "wb") as fh:
            fh.write(pages.pages[2 * i] + pages.pages[2 * i + 1])
        asm += [f"SECTION BANK_{bank}", f'BINARY "gfx/bank_{bank}.bin"']
    last = FIRST_BANK + nbanks - 1
    print(f"  {nbanks} banks ({nbanks * 16} K): {FIRST_BANK}..{last}")
    assert last <= 111, "more than the Next's 2 MB"
    open(os.path.join(PROJ, "src", "gfx_data.asm"), "w").write("\n".join(asm) + "\n")

    # ---- C
    bt = bounce_tables()
    hdr += ["/* made by tools/gen_data.py - do not edit */", "#ifndef GFX_DATA_H", "#define GFX_DATA_H",
            "#include <stdint.h>", "",
            "/* a view: the first 8K page of its stream, the screen point its rows are relative to */",
            "typedef struct { uint8_t page; int16_t cx, cy; } View;",
            "/* a picture: 256 x 96 texels in 3 pages; its palette (224 x 2 bytes) and tile classes",
            "   (3 grids x 256) at meta offset meta in the meta pages */",
            "typedef struct { const char *title; uint8_t level, room, page; uint16_t meta; } Pic;",
            "/* a room: 256 x 192 at 4 bits a pixel in 3 pages; its 32 palette entries at meta; shadow",
            "   0 on the back wall, 1 on the floor, 2 none */",
            "typedef struct { const char *title; uint8_t page; uint16_t meta; uint8_t shadow; } Room;", "",
            f"#define FIRST_DATA_BANK {FIRST_BANK}", f"#define LAST_DATA_BANK {last}",
            f"#define NPITCH {len(PITCHES)}", f"#define PITCH0 {PITCHES.index(0)}       /* the level view */",
            f"#define PITCH_DEFAULT {PITCHES.index(12)}  /* the original's 12 degrees */",
            f"#define NPICS {len(pic_recs) + 1}      /* 0: the Boing ball, made by the game */",
            f"#define NROOMS {len(room_recs)}",
            f"#define META_PAGE {meta_page}", f"#define META_PAGES {meta_pages}",
            f"#define NRING {len(ring)}",
            f"#define BOUNCE_T {len(bt['title'][0])}", f"#define SWAY_T {len(bt['title'][1])}",
            f"#define BOUNCE_S {len(bt['solved'][0])}", f"#define SWAY_S {len(bt['solved'][1])}",
            "/* render.c shadow(): on the back wall a disc up and right of the ball, above the floor line;",
            "   on the floor an ellipse under it (port: as dark at any height) */",
            ] + shadow_defines() + [
            "extern const int8_t pitch_deg[NPITCH];",
            "extern const View v_play[NPITCH], v_flip[NPITCH], v_fstat[NPITCH], v_title, v_select, v_ct;",
            "extern const Pic pics[NPICS];", "extern const Room rooms[NROOMS];",
            "extern const uint8_t ring_uv[NRING][2];   /* the flip ring: texels (u from the half's middle, v) */",
            "extern const int8_t bounce_t[BOUNCE_T], sway_t[SWAY_T], bounce_s[BOUNCE_S], sway_s[SWAY_S];",
            "extern const int8_t wall_dy_t[BOUNCE_T], wall_dx_t[SWAY_T], wall_dy_s[BOUNCE_S], wall_dx_s[SWAY_S];",
            "#endif"]

    def vc(name):
        if name not in vrec:
            return "{ 0, 0, 0 }"
        p, cx, cy, _, _ = vrec[name]
        return f"{{ {p}, {cx}, {cy} }}"

    csrc += ["/* made by tools/gen_data.py - do not edit */", '#include "gfx_data.h"', "",
             "const int8_t pitch_deg[NPITCH] = { " + ", ".join(str(p) for p in PITCHES) + " };",
             "const View v_play[NPITCH] = { " + ", ".join(vc(f"play{p}") for p in PITCHES) + " };",
             "const View v_flip[NPITCH] = { " + ", ".join(vc(f"flip{p}") for p in PITCHES) + " };",
             "const View v_fstat[NPITCH] = { " + ", ".join(vc(f"fstat{p}") for p in PITCHES) + " };",
             f"const View v_title = {vc('title')};", f"const View v_select = {vc('select')};",
             f"const View v_ct = {vc('ct')};", "",
             "const Pic pics[NPICS] = {", '    { "THE BOING BALL", 0, 0, 0, 0 },']
    for title, level, room, page, moff in pic_recs:
        ri = order.index(room) if room in order else 0
        csrc.append(f"    {{ {c_str(title)}, {level}, {ri}, {page}, {moff} }},")
    csrc += ["};", "", "const Room rooms[NROOMS] = {"]
    for rname, title, page, moff, sh in room_recs:
        csrc.append(f"    {{ {c_str(title)}, {page}, {moff}, {sh} }},")
    csrc += ["};", "", "const uint8_t ring_uv[NRING][2] = {"]
    csrc += ["    " + ", ".join(f"{{ {a}, {b} }}" for a, b in ring[i:i + 8]) + "," for i in range(0, len(ring), 8)]
    csrc += ["};", ""]

    def arr(name, vals):
        return [f"const int8_t {name}[{len(vals)}] = {{"] + \
               ["    " + ", ".join(str(v) for v in vals[i:i + 16]) + "," for i in range(0, len(vals), 16)] + ["};"]

    # the wall shadow follows the ball: (x + 0.85, y - 0.15) on the back wall, as screen offsets
    def wall(vals, scale):
        return [int(round(-FOCAL * v / -geom.ROOM_Z * scale)) for v in vals]

    csrc += arr("bounce_t", bt["title"][0]) + arr("sway_t", bt["title"][1])
    csrc += arr("bounce_s", bt["solved"][0]) + arr("sway_s", bt["solved"][1])
    csrc += arr("wall_dy_t", [int(round(-FOCAL * (v - 0.15) / 6.5 / 2)) for v in bt["title"][2]])
    csrc += arr("wall_dx_t", [int(round(FOCAL * (v + 0.85) / 6.5)) for v in bt["title"][3]])
    csrc += arr("wall_dy_s", [int(round(-FOCAL * (v - 0.15) / 6.5 / 2)) for v in bt["solved"][2]])
    csrc += arr("wall_dx_s", [int(round(FOCAL * (v + 0.85) / 6.5)) for v in bt["solved"][3]])
    open(os.path.join(PROJ, "src", "gfx_data.h"), "w").write("\n".join(hdr) + "\n")
    open(os.path.join(PROJ, "src", "gfx_data.c"), "w").write("\n".join(csrc) + "\n")
    print(f"  meta {len(meta)} bytes, {len(pic_recs)} pictures, {len(room_recs)} rooms")

    if args.preview:
        os.makedirs(args.preview, exist_ok=True)
        for name, idx, cols in previews:
            pal = np.array(list(cols) + [(0, 0, 0)] * (NCOL - len(cols)), np.uint8)
            Image.fromarray(pal[idx]).resize((512, 192), Image.NEAREST).save(os.path.join(args.preview, name + ".png"))
        for rname, ridx, rpal in room_imgs:
            pal = np.array(rpal + [0] * (48 - len(rpal)), np.uint8).reshape(16, 3)
            Image.fromarray(pal[ridx]).resize((512, 384), Image.NEAREST).save(
                os.path.join(args.preview, "room_" + rname + ".png"))


if __name__ == "__main__":
    main()
