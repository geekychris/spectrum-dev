// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * track.c - the road at any z: its centre line, height, direction, curve,
 * old lane lines, edges and lanes. (port: route_build and the routes'
 * sections are in route.c, banked)
 */
#include "sc.h"

const RouteInfo route_info[NUM_ROUTES] = {
    /* name            destination      AI      nl no limit traffic sky */
    { "LEVEE ROAD",    "THE MARINA",    "1.0",  1, 1,  80,  10, 0 },
    { "RUSH HOUR",     "THE OFFICE",    "2.0",  3, 0, 110,  34, 1 },
    { "MOUNTAIN PASS", "THE SKI LODGE", "3.0",  1, 1,  70,   8, 2 },
    { "FLOODED TOWN",  "HOME",          "4.0",  1, 1,  50,  12, 3 },
};

int seg_of(int32_t z)
{
    return (int)(z >> (FBITS + SEG_BITS));
}

static int clamp_seg(const World *w, int i)
{
    if (i < 0) return 0;
    if (i > w->nseg - 2) return w->nseg - 2;
    return i;
}

int32_t road_cx(const World *w, int32_t z)
{
    int i = seg_of(z), j = clamp_seg(w, i);
    int32_t f = (z >> FBITS) - ((int32_t)j << SEG_BITS);
    return w->seg[j].cx + ((w->seg[j].dir * f) >> SEG_BITS);
}

int16_t road_y(const World *w, int32_t z)            /* port: int16_t results */
{
    int j = clamp_seg(w, seg_of(z));
    int32_t f = (z >> FBITS) - ((int32_t)j << SEG_BITS);
    return w->seg[j].y + (((w->seg[j + 1].y - w->seg[j].y) * f) >> SEG_BITS);
}

int road_dir(const World *w, int32_t z)
{
    int j = clamp_seg(w, seg_of(z));
    int32_t f = (z >> FBITS) - ((int32_t)j << SEG_BITS);
    int32_t d = w->seg[j].dir + (((w->seg[j + 1].dir - w->seg[j].dir) * f) >> SEG_BITS);
    return sc_atan_ratio(d, SEG_LEN);
}

int16_t road_curve(const World *w, int32_t z)
{
    int j = clamp_seg(w, seg_of(z));
    return w->seg[j + 1].dir - w->seg[j].dir;
}

int16_t ghost_at(const World *w, int32_t z)
{
    int j = clamp_seg(w, seg_of(z));
    int32_t f = (z >> FBITS) - ((int32_t)j << SEG_BITS);
    if (!(w->seg[j].flags & SF_GHOST)) return 0;
    return w->seg[j].ghost + (((w->seg[j + 1].ghost - w->seg[j].ghost) * f) >> SEG_BITS);
}

int16_t road_left(const World *w)
{
    const RouteInfo *r = &route_info[w->route];
    return r->no ? -r->no * LANE_W : -8;
}

int16_t road_right(const World *w)
{
    return route_info[w->route].nl * LANE_W;
}

int16_t lane_centre(const World *w, int lane)
{
    (void)w;
    return lane * LANE_W + LANE_W / 2;
}

int lane_of(const World *w, int16_t d)
{
    int16_t u = d >> FBITS;
    int n = route_info[w->route].nl;
    if (u < 0) return -1;
    if (u >= n * LANE_W) return n - 1;
    return (int)(u / LANE_W);
}
