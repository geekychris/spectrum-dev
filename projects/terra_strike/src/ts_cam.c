// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ts_cam.c - the original ts_math.c's camera transform, projection,
 * polygon clipping, spans and radar mapping (split from ts_math.c: port:
 * the logic in ts_math.c runs from its own bank on the Next; these are for
 * the host tests and the radar).
 */
#include "ts.h"

#ifndef __SDCC          /* port: host tests only (render.c has the Next's) */
void cam_setup(Cam *c)
{
    c->cy = TS_COS(c->yaw);
    c->sy = TS_SIN(c->yaw);
    c->cp = TS_COS(c->pitch);
    c->sp = TS_SIN(c->pitch);
}

/* World point -> camera space. Deltas wrap, so the camera always sees the
 * nearest copy of the tiled world. Products stay below 2^31: |delta| is at
 * most 8192 and the trig values at most 65536. */
void cam_xform(const Cam *c, int32_t wx, int32_t wy, int32_t wz, V3 *out)
{
    int32_t dx = ts_wrap_delta(wx, c->x);
    int32_t dz = ts_wrap_delta(wz, c->z);
    int32_t dy = wy - c->y;
    int32_t rx, rz;

    rx = (dx * (c->cy >> 4) - dz * (c->sy >> 4)) >> 12;
    rz = (dx * (c->sy >> 4) + dz * (c->cy >> 4)) >> 12;
    out->x = rx;
    out->y = (dy * (c->cp >> 4) + rz * (c->sp >> 4)) >> 12;
    out->z = (rz * (c->cp >> 4) - dy * (c->sp >> 4)) >> 12;
}

void cam_project(const V3 *p, int32_t *sx, int32_t *sy)
{
    *sx = VIEW_CX + (p->x * FOV) / p->z;
    *sy = VIEW_CY - (p->y * FOV) / p->z;
}

int clip_near(V3 *in, int n, V3 *out)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        V3 *a = &in[i], *b = &in[(i + 1) % n];
        int ain = a->z >= NEAR_Z, bin = b->z >= NEAR_Z;
        if (ain && m < MAX_PV) out[m++] = *a;
        if (ain != bin && m < MAX_PV) {
            /* interpolate from the inside end, so a shared edge clips to the
             * same point in both polygons whichever way they wind it */
            V3 *p = ain ? a : b, *q = ain ? b : a;
            int32_t dz = q->z - p->z;
            int32_t t = NEAR_Z - p->z;
            out[m].x = p->x + (int32_t)(((int64_t)(q->x - p->x) * t) / dz);
            out[m].y = p->y + (int32_t)(((int64_t)(q->y - p->y) * t) / dz);
            out[m].z = NEAR_Z;
            m++;
        }
    }
    return m;
}

/* One Sutherland-Hodgman pass against an axis-aligned edge.
 * axis 0 = x, 1 = y; keep values >= lim when ge, <= lim otherwise. */
static int clip_edge(int32_t *xs, int32_t *ys, int n, int32_t *ox, int32_t *oy,
                     int axis, int32_t lim, int ge)
{
    int i, m = 0;
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        int32_t av = axis ? ys[i] : xs[i];
        int32_t bv = axis ? ys[j] : xs[j];
        int ain = ge ? av >= lim : av <= lim;
        int bin = ge ? bv >= lim : bv <= lim;
        if (ain && m < MAX_PV) { ox[m] = xs[i]; oy[m] = ys[i]; m++; }
        if (ain != bin && m < MAX_PV) {
            /* interpolate from the inside end (see clip_near) */
            int p = ain ? i : j, q = ain ? j : i;
            int32_t pv = axis ? ys[p] : xs[p], qv = axis ? ys[q] : xs[q];
            int64_t t = lim - pv, d = qv - pv;
            if (axis) {
                ox[m] = xs[p] + (int32_t)(((int64_t)(xs[q] - xs[p]) * t) / d);
                oy[m] = lim;
            } else {
                ox[m] = lim;
                oy[m] = ys[p] + (int32_t)(((int64_t)(ys[q] - ys[p]) * t) / d);
            }
            m++;
        }
    }
    return m;
}

int clip_rect(int32_t *xs, int32_t *ys, int n, int32_t x0, int32_t y0,
              int32_t x1, int32_t y1)
{
    int32_t tx[MAX_PV], ty[MAX_PV];
    int i, inside = 1;

    for (i = 0; i < n; i++)
        if (xs[i] < x0 || xs[i] > x1 || ys[i] < y0 || ys[i] > y1) { inside = 0; break; }
    if (inside) return n;

    n = clip_edge(xs, ys, n, tx, ty, 0, x0, 1); if (n < 3) return 0;
    n = clip_edge(tx, ty, n, xs, ys, 0, x1, 0); if (n < 3) return 0;
    n = clip_edge(xs, ys, n, tx, ty, 1, y0, 1); if (n < 3) return 0;
    n = clip_edge(tx, ty, n, xs, ys, 1, y1, 0); if (n < 3) return 0;
    return n;
}

#endif

void radar_map(int32_t dx, int32_t dz, int yaw, int scale_shift, int *rx, int *ry)
{
    int32_t c = TS_COS(yaw) >> 4, s = TS_SIN(yaw) >> 4;
    int32_t x = (dx * c - dz * s) >> 12;
    int32_t z = (dx * s + dz * c) >> 12;
    *rx = (int)(x >> scale_shift);
    *ry = (int)(-(z >> scale_shift));
}

#ifndef __SDCC
/* Scan-convert a convex polygon into per-row spans. Rows ymin..ymax get
 * xl[y - ymin]..xr[y - ymin] (inclusive). Returns the row count. Edges are
 * stepped in 16.16 fixed point; both end rows of each edge are included so
 * neighbouring polygons never leave a gap. */
int poly_spans(const int16_t *xs, const int16_t *ys, int n,
               int16_t *xl, int16_t *xr, int *ymin_out)
{
    int i, y, ymin = ys[0], ymax = ys[0], rows;

    for (i = 1; i < n; i++) {
        if (ys[i] < ymin) ymin = ys[i];
        if (ys[i] > ymax) ymax = ys[i];
    }
    rows = ymax - ymin + 1;
    if (rows > SPAN_MAX) rows = SPAN_MAX;
    for (y = 0; y < rows; y++) { xl[y] = 32767; xr[y] = -32768; }

    for (i = 0; i < n; i++) {
        int x0 = xs[i], y0 = ys[i], x1 = xs[(i + 1) % n], y1 = ys[(i + 1) % n];
        int32_t x, dx;
        if (y0 > y1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
        x = (int32_t)x0 << 16;
        dx = y1 > y0 ? (((int32_t)(x1 - x0)) << 16) / (y1 - y0) : 0;
        for (y = y0; y <= y1; y++, x += dx) {
            int r = y - ymin, px = (int)((x + 0x8000) >> 16);
            if (y == y1) px = x1;
            if (r < 0 || r >= rows) continue;
            if (px < xl[r]) xl[r] = (int16_t)px;
            if (px > xr[r]) xr[r] = (int16_t)px;
        }
    }
    *ymin_out = ymin;
    return rows;
}
#endif
