/*
 * The planet on Layer 2, as the original's render_cel.c draws it: the
 * camera hovers above the focus, tilted back, looking just ahead of it; the
 * planet's silhouette, then the walls (the side facing the camera and the
 * top), the crumbs and the nest, far to near by depth buckets.
 *
 * port: on the Z80 the walls aren't eight-cornered boxes projected point by
 * point. Every lattice corner on the camera's side of the horizon is
 * rotated (Q7 direction x Q14 matrix) and projected twice, at the ground
 * and at the walls' height (xf_corners, xform.asm); a wall is then its two
 * corners' points, its thickness a screen-space offset across it, and a
 * crumb sits between its cell's four corners. The screen is 256 x 192, the
 * original's 320 x 240 at 0.8 (focal length 246 * 0.8). Keys, spooks and
 * the chomper are hardware sprites (sprites.c), always in front of the
 * walls.
 */
#include <string.h>
#include "pc.h"
#include "game.h"
#include "render.h"
#include "rtab.h"

#define NEAR_Z    Q8(0.6)
#define TILT      148                 /* Q8(0.58) */
#define LOOKAHEAD 20                  /* Q8(0.08) */
#define CRUMB_R   Q8(0.13)
#define WALL_HALFT Q8(0.13)

int16_t rd_stats_walls, rd_stats_crumbs;
V3 rd_campos;

/* the camera: rows right, up, forward (Q14); its position in its own frame
   (Q8); the direction to it in Q7, for the horizon tests */
vec3 xf_m[3];
vec3 xf_c;
int8_t xf_chat[3];
int16_t xf_thr;                       /* corners below this dot (Q14) are round the back */
int16_t xf_thr_wall, xf_thr_crumb;    /* the same for a wall (its corners' mean), a crumb */
int8_t xf_fwd8[3];                    /* the view direction, Q7 (wall shading) */
int8_t xf_bot8[3];                    /* the bottom edge's plane: its normal, Q7 */
int16_t xf_thr_bot;                   /* ... and the corners' dot below which they're under it */
int8_t xf_m8[3][3];                   /* the rows in Q7 (xform.asm) */
#ifdef XF_ASM
void xf_setup(void);                  /* xform.asm: puts this camera into its code */
#else
#define xf_setup()
#endif

/* the corners (xf_corners): screen points at the ground (b) and the walls'
   top (t), the depth of the ground point, the horizon dot (rtab.h) */

/* ---- projection ---- */

static int16_t scale_at(int16_t z)    /* focal * 16384 / z */
{
    uint16_t i = (uint16_t)z >> 4;
    return rd_recip[i < RECIP_N ? i : RECIP_N - 1];
}

static int16_t pmul(int16_t v, int16_t s) { return (int16_t)(mul16(v, s) >> 14); }

uint8_t rd_project(const V3 *p, int16_t *sx, int16_t *sy, int16_t *z)
{
    V3 r, c;
    int16_t s;
    v3_sub(&r, p, &rd_campos);
    v3_mat(&c, xf_m, &r);
    if (c.z < NEAR_Z) return 0;
    s = scale_at(c.z);
    *sx = RD_CX + pmul(c.x, s);
    *sy = RD_CY - pmul(c.y, s);
    *z = c.z;
    return 1;
}

int16_t rd_size16(int16_t size, int16_t z) { return (int16_t)(mul16(size, scale_at(z)) >> 10); }

int16_t rd_horizon(const V3 *dir) { return v3_dot(dir, &rd_campos); }

#ifndef XF_ASM
static int16_t clampc(int16_t v) { return v < -2048 ? -2048 : v > 2047 ? 2047 : v; }

/* the reference for xform.asm's xf_corners: every corner on this side of
   the horizon, rotated (8-bit Q7 matrix, as the asm) and projected at the
   ground and the walls' top */

void xf_corners(void)
{
    int16_t i;
    for (i = 0; i < CORNERS; i++) {
        const uint8_t *u = &rd_corner8[i * 3];
        int16_t d[3], dot, q[3], b[3], t[3], s;
        uint8_t r;
        Corner *c = CN(CREC(i));
        d[0] = (int16_t)u[0] - 128; d[1] = (int16_t)u[1] - 128; d[2] = (int16_t)u[2] - 128;
        dot = d[0] * xf_chat[0] + d[1] * xf_chat[1] + d[2] * xf_chat[2];
        c->dot = dot;
        c->vis = 0;
        if (dot < xf_thr) continue;
        if (d[0] * xf_bot8[0] + d[1] * xf_bot8[1] + d[2] * xf_bot8[2] < xf_thr_bot) continue;
        for (r = 0; r < 3; r++)
            q[r] = d[0] * xf_m8[r][0] + d[1] * xf_m8[r][1] + d[2] * xf_m8[r][2];
        for (r = 0; r < 3; r++) {
            int16_t q3 = q[r] + (q[r] >> 1);                 /* 1.5 q: 3 q would pass 16 bits */
            b[r] = (q3 >> 3) - (&xf_c.x)[r];                /* the ground: R = 3/16 of Q14 in Q8 */
            t[r] = b[r] + (q3 >> 7);                         /* + 0.75 */
        }
        if (b[2] < NEAR_Z || t[2] < NEAR_Z) continue;
        s = scale_at(b[2]);
        c->bx = clampc(RD_CX + pmul(b[0], s));
        c->by = clampc(RD_CY - pmul(b[1], s));
        s = scale_at(t[2]);
        c->tx = clampc(RD_CX + pmul(t[0], s));
        c->ty = clampc(RD_CY - pmul(t[1], s));
        c->z = b[2];
        c->vis = 1;
    }
}
#endif

/* ---- the draw list: depth buckets of 1/4 unit, far to near ---- */

#define BUCKETS 256
#define MAX_ITEMS 360
enum { IT_WALL, IT_CRUMB, IT_NEST };
#ifndef XF_ASM
/* the C reference of draw.asm's list (the host; the corners live in mem.asm on the Next) */
Corner cn_host[CORNERS];
int16_t rd_bucket[BUCKETS];
int16_t it_next[MAX_ITEMS], it_a[MAX_ITEMS], it_b[MAX_ITEMS];
int8_t it_ox[MAX_ITEMS], it_oy[MAX_ITEMS];
uint8_t it_type[MAX_ITEMS], it_col[MAX_ITEMS];
static int16_t nitems;

static int16_t add_item(uint8_t type, int16_t z)
{
    int16_t b = z >> 6, n;
    if (nitems >= MAX_ITEMS) return -1;
    if (b < 0) b = 0;
    if (b >= BUCKETS) b = BUCKETS - 1;
    n = nitems++;
    it_type[n] = type;
    it_next[n] = rd_bucket[b];
    rd_bucket[b] = n;
    return n;
}
#endif

/* ---- the planet's silhouette: a circle where sight lines graze the
   sphere, the same on screen for a given camera height (cached) ---- */

static int16_t disc_h = -1, disc_xy[48];

static void disc_points(void)
{
    static const int16_t cs[24][2] = {
        {16384,0},{15827,4240},{14189,8192},{11585,11585},{8192,14189},{4240,15827},
        {0,16384},{-4240,15827},{-8192,14189},{-11585,11585},{-14189,8192},{-15827,4240},
        {-16384,0},{-15827,-4240},{-14189,-8192},{-11585,-11585},{-8192,-14189},{-4240,-15827},
        {0,-16384},{4240,-15827},{8192,-14189},{11585,-11585},{14189,-8192},{15827,-4240}
    };
    int32_t d2;
    int16_t dcam, c_r, rr, z;
    V3 chat, e1, e2, centre, p, t;
    uint8_t i;
    d2 = mul16(rd_campos.x >> 4, rd_campos.x >> 4) + mul16(rd_campos.y >> 4, rd_campos.y >> 4) +
         mul16(rd_campos.z >> 4, rd_campos.z >> 4);
    dcam = (int16_t)(isqrt32_fast((uint32_t)d2) << 4);
    v3_norm(&chat, &rd_campos);
    c_r = (int16_t)(mul16(PLANET_R, PLANET_R) / dcam);
    rr = (int16_t)isqrt32_fast((uint32_t)(mul16(PLANET_R, PLANET_R) - mul16(c_r, c_r)));
    v3_scale(&t, &chat, v3_dot(&xf_m[0], &chat));
    v3_sub(&t, &xf_m[0], &t);
    v3_norm(&e1, &t);
    v3_cross(&e2, &chat, &e1);
    v3_scale(&centre, &chat, c_r);
    for (i = 0; i < 24; i++) {
        p.x = centre.x + mulq14(e1.x, mulq14(rr, cs[i][0])) + mulq14(e2.x, mulq14(rr, cs[i][1]));
        p.y = centre.y + mulq14(e1.y, mulq14(rr, cs[i][0])) + mulq14(e2.y, mulq14(rr, cs[i][1]));
        p.z = centre.z + mulq14(e1.z, mulq14(rr, cs[i][0])) + mulq14(e2.z, mulq14(rr, cs[i][1]));
        if (!rd_project(&p, &disc_xy[i * 2], &disc_xy[i * 2 + 1], &z)) { disc_h = -2; return; }
    }
}

/* ---- the frame ---- */

static int8_t q7(int16_t v)            /* Q14 -> Q7, rounded */
{
    v = (v + 64) >> 7;
    return (int8_t)(v > 127 ? 127 : v);
}

static void camera(void)
{
    const Camera *cam = &g.cam;
    int16_t h = cam->height, i;
    V3 target, t, fwd, right, cup;
    int32_t dsq;
    /* campos = f (R + h) - up (h tilt); target = f R + up (h lookahead) */
    v3_scale(&rd_campos, &cam->focus, PLANET_R + h);
    v3_scale(&t, &cam->up, (int16_t)(mul16(h, TILT) >> 8));
    v3_sub(&rd_campos, &rd_campos, &t);
    v3_scale(&target, &cam->focus, PLANET_R);
    v3_scale(&t, &cam->up, (int16_t)(mul16(h, LOOKAHEAD) >> 8));
    v3_add(&target, &target, &t);
    v3_sub(&t, &target, &rd_campos);
    v3_norm(&fwd, &t);
    v3_cross(&t, &cam->up, &fwd);
    v3_norm(&right, &t);
    v3_cross(&cup, &fwd, &right);
    xf_m[0] = right;
    xf_m[1] = cup;
    xf_m[2] = fwd;
    v3_mat(&xf_c, xf_m, &rd_campos);
    /* the horizon: corners whose wall might show (the original's walls:
       mid . C >= R - 0.6, minus half a wall) */
    v3_norm(&t, &rd_campos);
    for (i = 0; i < 3; i++) {
        xf_chat[i] = q7((&t.x)[i]);
        xf_fwd8[i] = q7((&fwd.x)[i]);
        xf_m8[0][i] = q7((&right.x)[i]);
        xf_m8[1][i] = q7((&cup.x)[i]);
        xf_m8[2][i] = q7((&fwd.x)[i]);
    }
    /* port: the plane through the camera and the screen's bottom edge, normal
       focal * up + 96 * forward (/ 219); corners more than 2.5 units below
       it are skipped (a wall crossing it has both corners within a cell) */
    {
        V3 w, a;
        int16_t wc;
        v3_scale(&w, &cup, 14723);
        v3_scale(&a, &fwd, 7182);
        v3_add(&w, &w, &a);
        for (i = 0; i < 3; i++) xf_bot8[i] = q7((&w.x)[i]);
        wc = v3_dot(&w, &rd_campos);                                  /* Q8 */
        xf_thr_bot = (int16_t)(((int32_t)(wc - Q8(2.5)) << 14) / PLANET_R);
    }
    xf_setup();
    dsq = mul16(rd_campos.x >> 4, rd_campos.x >> 4) + mul16(rd_campos.y >> 4, rd_campos.y >> 4) +
          mul16(rd_campos.z >> 4, rd_campos.z >> 4);
    {
        int16_t dist = (int16_t)(isqrt32_fast((uint32_t)dsq) << 4);          /* |C|, Q8 */
        xf_thr = (int16_t)(((int32_t)(PLANET_R - Q8(0.6)) << 14) / dist) - 1500;
        xf_thr_wall = (int16_t)(((int32_t)(PLANET_R - Q8(0.6)) << 14) / dist);
        xf_thr_crumb = (int16_t)(((int32_t)PLANET_R << 14) / dist) - 160;   /* the cell centre is ~1% inside its corners' */
    }
    if (h != disc_h) { disc_h = h; disc_points(); }
}

/* a wall: its two corners' ground and top points; the side facing the
   camera and the top, the thickness across the wall's top edge */
#ifndef XF_ASM
static int16_t tsize(int16_t z) { uint16_t i = (uint16_t)z >> 7; return rd_tsize[i < 128 ? i : 127]; }

/* q = M d for an offset-binary Q7 direction (the rows xform.asm uses) */
static int16_t row8(const int8_t *m, const int8_t *d) { return m[0] * d[0] + m[1] * d[1] + m[2] * d[2]; }

/* a wall: its two corners' ground and top points; the side facing the
   camera and the top; its thickness the side normal through the camera at
   the wall's depth, toward the ground points (the side the camera sees) */
static void add_wall(int16_t e)
{
    const Edge *ed = &ed_rec[e];
    const Corner *ca = CN(ed->a), *cb = CN(ed->b);
    int16_t z, t, qx, qy, qz, ox, oy, n, k;
    int32_t face;
    if (!ca->vis || !cb->vis) return;
    if ((ca->dot >> 1) + (cb->dot >> 1) < xf_thr_wall) return;   /* (sums of two pass 16 bits) */
    z = (ca->z >> 1) + (cb->z >> 1);
    n = add_item(IT_WALL, z);
    if (n < 0) return;
    t = tsize(z);
    qx = row8(xf_m8[0], ed->side);
    qy = row8(xf_m8[1], ed->side);
    qz = row8(xf_m8[2], ed->side);
    ox = (int16_t)((int32_t)qx * t >> 14);                    /* 1/16 px */
    oy = (int16_t)(-(int32_t)qy * t >> 14);
    face = (int32_t)ox * (ca->bx - ca->tx + cb->bx - cb->tx) + (int32_t)oy * (ca->by - ca->ty + cb->by - cb->ty);
    if (face < 0) { ox = -ox; oy = -oy; }
    it_a[n] = (int16_t)ed->a;
    it_b[n] = (int16_t)ed->b;
    it_ox[n] = (int8_t)((ox + 8) >> 4);
    it_oy[n] = (int8_t)((oy + 8) >> 4);
    /* shade: the original's 120 + 150 |side . fwd| */
    k = (qz < 0 ? -qz : qz) >> 11;
    if (k > 7) k = 7;
    it_col[n] = C_WALL_SIDE + (uint8_t)k;
    rd_stats_walls++;
}

/* a crumb: between its cell's four corners, 2/3 of the way up the walls
   (crumbs float at 0.5, walls are 0.75) */
static void add_crumb(int16_t c)
{
    const uint16_t *cc = &rd_cell_corner[c * 4];
    const Corner *a = CN(cc[0]), *d = CN(cc[2]);
    int16_t n, x, y, z, r;
    if (!a->vis || !d->vis) return;
    if ((a->dot >> 1) + (d->dot >> 1) < xf_thr_crumb) return;
    if (a->tx < -512 || a->tx > 767 || a->ty < -512 || a->ty > 703) return;   /* far off screen */
    z = (a->z >> 1) + (d->z >> 1);
    n = add_item(IT_CRUMB, z);
    if (n < 0) return;
    /* port: between two corners across the cell, 3/4 of the way up the
       walls (0.56; the original's crumbs float at 0.5, 2/3): x16 = 2 (sum b
       + 3 sum t), no division */
    x = 2 * (a->bx + d->bx + 3 * (a->tx + d->tx));
    y = 2 * (a->by + d->by + 3 * (a->ty + d->ty));
    r = tsize(z);                                         /* 1/16 px */
    if (r < 8) r = 8;
    it_a[n] = (x - r) >> 4;                  /* left, top, size */
    it_b[n] = (y - r) >> 4;
    r = (2 * r + 8) >> 4;
    it_ox[n] = (int8_t)(r < 1 ? 1 : r > 8 ? 8 : r);
    it_col[n] = C_CRUMB;
    rd_stats_crumbs++;
}

#endif

/* the nest: a flat violet disc on the surface (precomputed rim directions);
   drawn straight after the planet: a disc on the ground hides nothing
   standing on the ground behind it (port: the original sorts it) */
static int16_t nest_xy[16];

static void draw_nest(void)
{
    int16_t i, z;
    if (rd_horizon(&mz_spot[4]) < PLANET_R - Q8(1.25)) return;
    for (i = 0; i < 8; i++)
        if (!rd_project(&rd_nest_rim[i], &nest_xy[i * 2], &nest_xy[i * 2 + 1], &z)) return;
    dr_poly(nest_xy, 8, C_NEST);
}

#ifdef __SDCC
extern void rd_lap(uint8_t k);
#else
#define rd_lap(k)
#endif

#ifdef XF_ASM
/* draw.asm */
void rd_lists_reset(void);
void rd_crumbs(void);
void rd_walls(void);
void rd_draw(void);

void rd_frame(void)
{
    camera();
    rd_lap(0);
    rd_map_corners();
    xf_corners();
    rd_lap(1);
    rd_lists_reset();
    rd_crumbs();
    rd_lap(2);
    rd_map_walls();
    rd_walls();
    rd_lap(3);
    dr_begin();
    if (disc_h >= 0 && !(rd_dbg & 1)) dr_poly(disc_xy, 24, C_PLANET);
    draw_nest();
    if (!(rd_dbg & 2)) rd_draw();
    dr_end();
    rd_lap(4);
}
#else
void rd_frame(void)
{
    int16_t i, b;
    camera();
    rd_lap(0);
    rd_map_corners();
    xf_corners();
    rd_lap(1);
    for (i = 0; i < BUCKETS; i++) rd_bucket[i] = -1;
    nitems = 0;
    rd_stats_walls = rd_stats_crumbs = 0;
    for (i = 0; i < CELLS; i++) if (crumb[i]) add_crumb(i);
    rd_lap(2);
    rd_map_walls();
    for (i = 0; i < rd_nwalls; i++) add_wall(rd_wall[i]);
    rd_lap(3);
    dr_begin();
    if (disc_h >= 0 && !(rd_dbg & 1)) dr_poly(disc_xy, 24, C_PLANET);
    draw_nest();
    for (b = BUCKETS - 1; b >= 0; b--)
        for (i = rd_bucket[b]; i >= 0; i = it_next[i]) {
            int16_t a = it_a[i], c = it_b[i], ox = it_ox[i], oy = it_oy[i], xy[8];
            switch (it_type[i]) {
            case IT_WALL: {
                const Corner *p = CN(a), *q = CN(c);
                if (rd_dbg & 2) break;
                xy[0] = p->tx + ox; xy[1] = p->ty + oy;
                xy[2] = q->tx + ox; xy[3] = q->ty + oy;
                xy[4] = q->bx + ox; xy[5] = q->by + oy;
                xy[6] = p->bx + ox; xy[7] = p->by + oy;
                dr_poly(xy, 4, it_col[i]);
                xy[4] = q->tx - ox; xy[5] = q->ty - oy;
                xy[6] = p->tx - ox; xy[7] = p->ty - oy;
                dr_poly(xy, 4, C_WALL_TOP);
                break;
            }
            case IT_CRUMB:
                if (rd_dbg & 4) break;
                dr_rect(c, c + ox, a, a + ox - 1, it_col[i]);
                break;
            }
        }
    dr_end();
    rd_lap(4);
}
#endif

