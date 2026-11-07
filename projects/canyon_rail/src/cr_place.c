/*
 * cr_place.c - where things are in Canyon Rail (the original's cr_core.c,
 * moved here unchanged: port): the ride's line and the canyon's width, the
 * camera on it and the projection through it, and the places of targets,
 * the guardian's nodes and bolts. Resident: the rules (cr_core.c, banked)
 * and the picture both use it.
 */
#include <string.h>
#include "cr.h"

#define FOV        200
#define NODE_R     110
#define BOSS_AHEAD 450

const char *const cr_event_names[CRE_COUNT] = {
    "NONE", "SHOT", "HIT", "KILL", "BOLT_DOWN", "MISS", "HURT", "POD", "FIRE", "BOSS", "NODE", "BOSS_DOWN",
    "STAGE", "OVER", "WIN"
};

int cr_pop_event(CRGame *g, CREvent *e)
{
    if (!g->nev) return 0;
    *e = g->ev[0];
    memmove(g->ev, g->ev + 1, (g->nev - 1) * sizeof(CREvent));
    g->nev--;
    return 1;
}

static int32_t s16(int32_t amp, int32_t angle) { return (amp * a3_sin((int)(angle & 0xFFFF))) >> 16; }

int32_t cr_path_x(int32_t s) { return s16(260, s * 9) + s16(110, s * 23 + 9000); }
int32_t cr_path_y(int32_t s) { return s16(40, s * 7) + 60; }
int32_t cr_half_width(int32_t s) { return 160 + s16(40, s * 13); }
int cr_size(int type) { return type == TG_TURRET ? 22 : type == TG_DRONE ? 16 : 14; }

static void camera(int32_t s, Cam *c)
{
    c->x = cr_path_x(s);
    c->y = cr_path_y(s) + 40;
    c->z = s;
    c->yaw = a3_atan2(cr_path_x(s + 80) - cr_path_x(s), 80);
    c->pitch = 300;
    cam_view(c, CR_VIEW_W, CR_VIEW_H, CR_VIEW_W / 2, CR_VIEW_H / 2, FOV);
    cam_setup(c);
}

/* port: the camera depends only on cam_s; cr_project asks for it for every
   point, so it is made once per place (the same values) */
static Cam cam_cache;
static int32_t cam_cache_s = -1;

void cr_camera(const CRGame *g, Cam *c)
{
    if (g->cam_s != cam_cache_s) {
        camera(g->cam_s, &cam_cache);
        cam_cache_s = g->cam_s;
    }
    *c = cam_cache;
}

int cr_project(const CRGame *g, int32_t x, int32_t y, int32_t z, int *px, int *py, int32_t *depth)
{
    Cam c;
    V3 v;
    int32_t sx, sy;
    cr_camera(g, &c);
    cam_xform(&c, x, y, z, &v);
    if (v.z < NEAR_Z + 4) return 0;
    cam_project(&c, &v, &sx, &sy);
    *px = (int)sx;
    *py = (int)sy;
    *depth = v.z;
    return 1;
}

void cr_target_pos(const CRGame *g, int k, int32_t *x, int32_t *y, int32_t *z)
{
    const Target *t = &g->tg[k];
    *z = t->s;
    switch (t->type) {
    case TG_TURRET:
        *x = cr_path_x(t->s) + t->side * (cr_half_width(t->s) - 14);
        *y = 30 + t->h;
        break;
    case TG_DRONE:
        *x = cr_path_x(t->s) + s16(t->amp, g->tick * 500 + t->phase);
        *y = cr_path_y(t->s) + 70 + s16(25, g->tick * 900 + t->phase);
        break;
    default:
        *x = cr_path_x(t->s) + s16(60, g->tick * 400 + t->phase);
        *y = cr_path_y(t->s) + 45;
        break;
    }
}

void cr_node_pos(const CRGame *g, int k, int32_t *x, int32_t *y, int32_t *z)
{
    int32_t bs = CR_LEN + BOSS_AHEAD;
    uint16_t a = (uint16_t)((g->boss_angle + (int32_t)k * 16384) & 0xFFFF);     /* port: (int32_t) */
    *x = cr_path_x(bs) + ((a3_cos(a) * NODE_R) >> 16);
    *y = cr_path_y(bs) + 90 + ((a3_sin(a) * NODE_R) >> 16);
    *z = bs;
}

void cr_bolt_pos(const CRGame *g, int k, int32_t *x, int32_t *y, int32_t *z)
{
    const Bolt *b = &g->bolt[k];
    /* port: FDIV(FROMI(t), FROMI(flight)) is (t << 16) / flight exactly, and
       FMUL(FROMI(d), f) is d * f: the same values in 32 bits (|d| < 2000) */
    int32_t f = ((int32_t)b->t << 16) / b->flight;
    *x = (b->x0 + ((b->x1 - b->x0) >> 16) * f) >> 16;
    *y = (b->y0 + ((b->y1 - b->y0) >> 16) * f) >> 16;
    *z = (b->z0 + ((b->z1 - b->z0) >> 16) * f) >> 16;
}
