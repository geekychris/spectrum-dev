/*
 * The route: the title's flyover point, progress along the course, the
 * autopilot (GameDirector.DemoInput) and where a fall puts you back - the
 * original's game.c functions, unchanged but for the port: notes there.
 * port: in its own bank (game.c's code fills one); main.c's stubs
 * (path_at, course_progress, demo_input, demo_resync, respawn_point) map it
 * for game.c and the HUD.
 */
#include "game.h"

#define V3CPY(d, s) ((d).x = (s).x, (d).y = (s).y, (d).z = (s).z)

void path_at_b(long t, V3 *ap)                       /* t: Q16 along the route; port: fills *ap */
{
    long f = (t >> 4) * (C.npath - 1), i = f >> 12, k = f & 4095;
    V3 a;
    if (C.npath < 2) { V3CPY(*ap, C.spawn); return; }
    if (i >= C.npath - 1) { i = C.npath - 2; k = 4096; }
    a.x = C.path[i].x + (((C.path[i + 1].x - C.path[i].x) * k) >> 12);
    a.y = C.path[i].y + (((C.path[i + 1].y - C.path[i].y) * k) >> 12) + 2 * FX;
    a.z = C.path[i].z + (((C.path[i + 1].z - C.path[i].z) * k) >> 12);
    V3CPY(*ap, a);
}

long course_progress_b(const Player *p)
{
    long i, best = 0, bd = 0x7FFFFFFF;
    for (i = 0; i < C.npath; i++) {
        long dx = (C.path[i].x - p->m.p.x) >> 8, dy = (C.path[i].y - p->m.p.y) >> 8, dz = (C.path[i].z - p->m.p.z) >> 8;
        long d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    return C.npath ? C.path[best].progress : 0;
}

void demo_input_b(Player *p, fix *wx, fix *wz)
{
    V3 d;
    fix l, tx, tz, ex, ez;
    while (p->demo_wp < C.npath - 1) {
        d.x = C.path[p->demo_wp].x - p->m.p.x; d.y = 0; d.z = C.path[p->demo_wp].z - p->m.p.z;
        if (vlenp(&d) < 2 * FX) p->demo_wp++; else break;
    }
    d.x = C.path[p->demo_wp].x - p->m.p.x; d.y = 0; d.z = C.path[p->demo_wp].z - p->m.p.z;
    l = vlenp(&d);
    if (l < 16) { *wx = *wz = 0; return; }
    tx = (d.x << 12) / l; tz = (d.z << 12) / l;
    ex = ((tx * 34816) >> 12) - p->m.v.x;                  /* cruise at 8.5 m/s */
    ez = ((tz * 34816) >> 12) - p->m.v.z;
    d.x = ex; d.z = ez;
    l = vlenp(&d);
    if (l < 64) { *wx = tx; *wz = tz; return; }
    *wx = (ex << 12) / l; *wz = (ez << 12) / l;
}

void demo_resync_b(Player *p)
{
    long i, bd = 0x7FFFFFFF;
    for (i = 0; i < C.npath; i++) {
        long dx = (C.path[i].x - p->m.p.x) >> 8, dy = (C.path[i].y - p->m.p.y) >> 8, dz = (C.path[i].z - p->m.p.z) >> 8;
        long d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; p->demo_wp = (int)i; }
    }
}

void respawn_point_b(const Player *p, V3 *rp)        /* port: fills *rp */
{
    long i, best = -1, bd = 0x7FFFFFFF;
    V3 r;
    for (i = 0; i < C.npath; i++) {
        long dx, dy, dz, d;
        if (!C.path[i].safe) continue;                     /* never back out over a gap */
        dx = (C.path[i].x - p->m.last_ground.x) >> 8;
        dy = (C.path[i].y - p->m.last_ground.y) >> 8;
        dz = (C.path[i].z - p->m.last_ground.z) >> 8;
        d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) { V3CPY(*rp, C.spawn); return; }
    /* a step back along the route, so you come back on before whatever got you */
    for (i = best - 1; i >= 0; i--)
        if (C.path[i].safe) { best = i; break; }
    r.x = C.path[best].x; r.y = C.path[best].y + 1638; r.z = C.path[best].z;
    V3CPY(*rp, r);
}

