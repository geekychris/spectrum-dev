/* rr_ground.c - Ridge Runner's ground and course: the original's rr_centre,
 * rr_height and rr_on_track (rr_core.c), unchanged. port: a file of their
 * own, in the main memory, because the rules and the view (banked apart)
 * both use them (tools/port_core.py). */
#include "rr.h"

static int32_t s16(int32_t amp, int32_t angle) { return (amp * a3_sin((int)(angle & 0xFFFF))) >> 16; }

int32_t rr_centre(const RRGame *g, int32_t z)
{
    return s16(g->c1, z * 13 + g->ph1) + s16(g->c2, z * 34 + g->ph2);
}

int32_t rr_height(const RRGame *g, int32_t x, int32_t z)
{
    int32_t d = x - rr_centre(g, z), h;
    if (d < 0) d = -d;
    h = s16(g->a1, z * 20 + x * 7 + g->hph) + s16(g->a2, x * 29 - z * 17) + s16(g->a3, z * 156);
    if (d > RR_SHOULDER) h += (d - RR_SHOULDER) * 3 / 5;        /* the valley's sides, past a rough shoulder */
    return h;
}

int rr_on_track(const RRGame *g, int32_t x, int32_t z)
{
    int32_t d = x - rr_centre(g, z);
    return d > -RR_TRACK_HW && d < RR_TRACK_HW;
}

