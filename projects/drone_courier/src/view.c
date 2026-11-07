/*
 * view.c - Drone Courier's 3D view (the original's render.c scene(): the sky
 * and the ground, the towers near the drone and ahead of the camera, the
 * pads, the beacon over the next one, the drone and its shadow) on the
 * Next's Layer 2, filled by the SDK's l2poly.
 *
 * The original's camera, culling and draw order: a chase camera 170 behind
 * and 70 above the drone, looking down at the ground under it; the grid
 * cells within view_range (560, the 68k's) that are not behind the camera;
 * a tower is its roof and the walls facing the camera; polygons are sorted
 * by depth (the average of their corners' plus a bias) into buckets and
 * drawn far to near.
 *
 * port: the original's 320 x 256 view at 4/5 of its size: Layer 2's
 * 256 x 192, drawn 96 rows high and shown twice each by the copper (the
 * fill is mostly a cost per row). Its HUD is on the other layers (hud.c),
 * around the view as on the original's screen.
 *
 * port: the transform is the engine's cam_xform() in 16-bit pieces
 * (mul16s, the Z80N's MUL); a tower's four corners are made from its
 * column's and row's terms (the grid is axis-aligned: rx and rz are sums
 * of a term in x and a term in z), and each corner is projected once
 * (persp.h: a division a point) for the roof and the walls that share it.
 */
#include <string.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nextcopper.h"
#include "l2poly.h"
#include "l2double.h"
#include "persp.h"
#include "view.h"
#include "gfx_data.h"
#include "drone_data.h"

extern int32_t mul16s(int16_t a, int16_t b);

/* render.c's pens (Layer 2 palette indices: gfx_data.c) */
#define PEN_GROUND 5
#define PEN_PARK   6
#define PEN_TOWER  7
#define PEN_ROOF   13
#define PEN_DEPOT  14
#define PEN_CUST   15
#define PEN_BEACON 16
#define PEN_DRONE  17
#define PEN_ROTOR  19
#define PEN_PARCEL 20
#define PEN_FRAGILE 21
#define PEN_SHADOW 22

#define VW     256
#define FOV    160                      /* the original's 200, at 4/5 */
#define VCX    128                      /* its centre (160, 110), at 4/5 ... */
#define VCY    44                       /* ... and half the rows */
#define NEARZ  12                       /* the engine's NEAR_Z */
#define RANGE  440                      /* port: render.c view_range (560 on the 68k, 1400 on OS4) */
#define POOL   110                      /* (view_asm.asm has the same) */
#define NB     256                      /* depth buckets of 16 units (the engine's: 512 over 4000) */

/* a point: camera space; its projection and outcode, made when a polygon
   first uses it (prep) */
typedef struct { int16_t x, y, z, sx, sy; uint8_t oc; } PV;
#define OC_NEAR  1                      /* z < NEARZ */
#define OC_R     2                      /* x > z (the engine's cull) */
#define OC_L     4                      /* x < -z */
#define OC_SL    8                      /* on screen: left of it, */
#define OC_SR    16                     /* right, */
#define OC_ST    32                     /* above, */
#define OC_SB    64                     /* below (with z >= NEARZ) */
#define OC_TODO  128                    /* not made yet */
typedef struct { int16_t xy[16]; uint8_t n, col; uint8_t next; } DP;
#define NIL 255
#define recip ((const uint16_t *)0x6000)    /* page RECIP_PAGE, mapped while building */

/* port: the polygon pool and the buckets at fixed places in this file's
   bank (21, mapped at 0x0000 while drawing; the Makefile checks the code
   ends below VIEW_RAM) */
#define VIEW_RAM 0x2C00
#define pool  ((DP *)VIEW_RAM)
#define bhead ((uint8_t *)(VIEW_RAM + POOL * sizeof(DP)))
#define btail (bhead + NB)
uint8_t vw_npool;                       /* (view_asm.asm uses it) */
#define npool vw_npool
#define NPV 32
#define pv ((PV *)(btail + NB))           /* 32 points, then view_asm.asm's row buffers (to 0x3F2A) */

int16_t camx, camy, camz;                /* (view_asm.asm too) */
int16_t cy12, sy12, cp12, sp12;  /* the camera's cos/sin of yaw and pitch, 4096 = 1 */
static int16_t k_xs, k_ys, k_zs, k_xc, k_yc, k_zc;               /* the terms' factors, 4096 = 1 */

uint8_t view_towers, view_polys;
uint16_t view_t_build, view_t_fill;      /* raster lines: making the list, filling */

uint16_t view_cam_yaw;

/* ------------------------------------------------------------ the city's shape */

/* dc_core.c's dc_goal() and dc_ground() (that file is in another bank) */
static int16_t goal_of(const DCGame *g) { return g->carrying ? g->target : g->depot; }

static int16_t ground_at(const DCGame *g, fix x, fix z)
{
    int16_t ix = (int16_t)(x >> 16) + DC_HALF, iz = (int16_t)(z >> 16) + DC_HALF, c, r;
    int32_t d;
    if (ix < 0 || iz < 0 || ix >= DC_N * DC_PITCH || iz >= DC_N * DC_PITCH) return 0;
    c = ix / DC_PITCH;
    r = iz / DC_PITCH;
    d = x - FROMI(-DC_HALF + c * DC_PITCH + DC_PITCH / 2);
    if (d < 0) d = -d;
    if (d >= FROMI(DC_FOOT)) return 0;
    d = z - FROMI(-DC_HALF + r * DC_PITCH + DC_PITCH / 2);
    if (d < 0) d = -d;
    if (d >= FROMI(DC_FOOT)) return 0;
    return g->height[r][c];
}

/* ------------------------------------------------------------ the camera */

/* the engine's cam_xform (cam.precise = 0: whole units): view_asm.asm */
extern void xform(int16_t wx, int16_t wy, int16_t wz, PV *o);

/* the projection where view_asm.asm's prep has no table: z < 24 (the
   scale passes 32767), z >= 4096; and for clip_near's new points */
void project_c(PV *p) __z88dk_fastcall
{
    if (p->z < 24) {
        /* half the scale, then twice the product (persp_mul's s < 32768) */
        uint16_t s = recip[p->z] >> 1;
        int16_t sx = persp_mul(p->x, s), sy = persp_mul(p->y, s);
        sx = sx > 7900 ? 7900 : sx < -7900 ? -7900 : sx;
        p->sx = VCX + sx * 2;
        p->sy = VCY - (sy > 15900 ? 15900 : sy < -15900 ? -15900 : sy);
    } else {
        uint16_t s = p->z < 4096 ? recip[p->z] : persp_scale((uint16_t)p->z);
        p->sx = VCX + persp_mul(p->x, s);
        p->sy = VCY - (persp_mul(p->y, s) >> 1);
    }
}

/* view_asm.asm: a point's projection and outcode; a quad into the pool
   (those with a corner behind the near plane: quad_clip below) */
extern void prep(PV *p) __z88dk_fastcall;
extern void fastpoly(const DP *d) __z88dk_fastcall;
extern void quad(uint8_t ia, uint8_t ib, uint8_t ic, uint8_t id, int16_t bias, uint8_t col);

/* ------------------------------------------------------------ polygons */

/* view_asm.asm: a point's projection and outcode; a quad into the pool
   (those with a corner behind the near plane: quad_clip below) */
extern void prep(PV *p) __z88dk_fastcall;
extern void quad(uint8_t ia, uint8_t ib, uint8_t ic, uint8_t id, int16_t bias, uint8_t col);

/* ------------------------------------------------------------ polygons */


/* the engine's a3_quad for a quad with a corner behind the near plane
   (view_asm.asm's quad has prepared its points and culled it): clip_near_at,
   interpolated from the inside end, the new points projected at z = NEARZ
   (port: x * FOV / NEARZ as one multiply), straight into the pool */
#define NEAR_K 27307                     /* FOV * 2048 / NEARZ */
static int16_t clamp16k(int32_t v) { return v > 16000 ? 16000 : v < -16000 ? -16000 : (int16_t)v; }

static int16_t cx_[5], cy_[5], cz_[5], csx[5], csy[5];      /* the quad's corners, the first again */

void quad_clip(uint8_t ia, uint8_t ib, uint8_t ic, uint8_t id, int16_t bias, uint8_t col)
{
    DP *d = &pool[npool];
    int16_t *o = d->xy, minx = 32767, maxx = -32767, miny = 32767, maxy = -32767, depth, sx, sy;
    uint8_t i, n = 0, b;
    /* (copied out of the points: zsdcc is slow through struct pointers) */
    const uint8_t idx[4] = { ia, ib, ic, id };
    for (i = 0; i < 4; i++) {
        const PV *p = &pv[idx[i]];
        cx_[i] = p->x; cy_[i] = p->y; cz_[i] = p->z; csx[i] = p->sx; csy[i] = p->sy;
    }
    cx_[4] = cx_[0]; cy_[4] = cy_[0]; cz_[4] = cz_[0]; csx[4] = csx[0]; csy[4] = csy[0];
    depth = ((cz_[0] + cz_[1] + cz_[2] + cz_[3]) >> 2) + bias;
    for (i = 0; i < 4; i++) {
        uint8_t ain = cz_[i] >= NEARZ, bin = cz_[i + 1] >= NEARZ;
        if (ain) {
            sx = csx[i];
            sy = csy[i];
            *o++ = sx;
            *o++ = sy;
            n++;
            if (sx < minx) minx = sx;
            if (sx > maxx) maxx = sx;
            if (sy < miny) miny = sy;
            if (sy > maxy) maxy = sy;
        }
        if (ain != bin) {
            int16_t f, x, y;
            uint8_t pi = ain ? i : i + 1, qi = ain ? i + 1 : i;
            /* t / dz once, as a 4096ths fraction (one division) */
            f = (int16_t)(((int32_t)(NEARZ - cz_[pi]) << 12) / (cz_[qi] - cz_[pi]));
            x = cx_[pi] + (int16_t)(mul16s(cx_[qi] - cx_[pi], f) >> 12);
            y = cy_[pi] + (int16_t)(mul16s(cy_[qi] - cy_[pi], f) >> 12);
            sx = VCX + clamp16k(mul16s(x, NEAR_K) >> 11);
            sy = VCY - clamp16k(mul16s(y, NEAR_K) >> 12);
            *o++ = sx;
            *o++ = sy;
            n++;
            if (sx < minx) minx = sx;
            if (sx > maxx) maxx = sx;
            if (sy < miny) miny = sy;
            if (sy > maxy) maxy = sy;
        }
    }
    if (n < 3 || maxx < 0 || minx >= VW || maxy < 0 || miny >= VIEW_ROWS) return;
    d->n = n;
    d->col = col;
    d->next = NIL;
    b = depth < 0 ? 0 : depth >= NB * 16 ? NB - 1 : (uint8_t)(depth >> 4);
    if (bhead[b] == NIL) bhead[b] = npool;
    else pool[btail[b]].next = npool;
    btail[b] = npool;
    npool++;
}

/* a quad of four world points (pv[0..3]) */
static void wquad(int16_t x0, int16_t y0, int16_t z0, int16_t x1, int16_t y1, int16_t z1,
                  int16_t x2, int16_t y2, int16_t z2, int16_t x3, int16_t y3, int16_t z3, int16_t bias, uint8_t col)
{
    uint8_t i;
    xform(x0, y0, z0, &pv[0]);
    xform(x1, y1, z1, &pv[1]);
    xform(x2, y2, z2, &pv[2]);
    xform(x3, y3, z3, &pv[3]);
    for (i = 0; i < 4; i++) pv[i].oc = OC_TODO;
    quad(0, 1, 2, 3, bias, col);
}


/* ------------------------------------------------------------ points along axes */

/* port: render.c's P(a, b, yy) is the drone's position plus a forward and
   b to its right: in camera space, the drone's point plus a, b and the
   height difference times three axes (made once, in 256ths), so its 32
   points cost no transform each; each is made once for all the faces that
   share it */


/* camera-space axes in 256ths: a (the drone's forward, or the world's x),
   b (its right, or z), and up; points made from a base point and offsets
   along them (16-bit products: offsets up to ~120) */
static PV pbase;
static int16_t ax_x, ax_y, ax_z, bx_x, bx_y, bx_z, yx_y, yx_z;

static void city_axes(void)
{
    ax_x = k_xc >> 4; ax_y = k_ys >> 4; ax_z = k_zs >> 4;
    bx_x = k_xs >> 4; bx_y = k_yc >> 4; bx_z = k_zc >> 4;
    yx_y = cp12 >> 4;
    yx_z = -(sp12 >> 4);
}

/* a direction (Q12 world x, z) in camera space, 256ths */
static void axis(int16_t wx, int16_t wz, int16_t *ox, int16_t *oy, int16_t *oz)
{
    int16_t rz = (int16_t)((mul16s(wx, sy12) + mul16s(wz, cy12)) >> 12);
    *ox = (int16_t)((mul16s(wx, cy12) - mul16s(wz, sy12)) >> 16);
    *oy = (int16_t)(mul16s(rz, sp12) >> 16);
    *oz = (int16_t)(mul16s(rz, cp12) >> 16);
}

static void dpt(uint8_t i, int16_t a, int16_t b, int16_t dy)
{
    PV *p = &pv[i];
    p->x = pbase.x + ((a * ax_x + b * bx_x + 128) >> 8);
    p->y = pbase.y + ((a * ax_y + b * bx_y + dy * yx_y + 128) >> 8);
    p->z = pbase.z + ((a * ax_z + b * bx_z + dy * yx_z + 128) >> 8);
    p->oc = OC_TODO;
}

/* render.c flat(): a level square, half sizes hx, hz, round (cx, y, cz),
   from the base (pbase at (cx, y0, cz)), dy above it */
static void aflat(int16_t hx, int16_t hz, int16_t dy, int16_t bias, uint8_t col)
{
    dpt(0, -hx, -hz, dy);
    dpt(1, hx, -hz, dy);
    dpt(2, hx, hz, dy);
    dpt(3, -hx, hz, dy);
    quad(0, 1, 2, 3, bias, col);
}

/* ------------------------------------------------------------ the city */

/* port: cam_xform is linear in the world point, so a tower corner's camera
   coordinates are sums of a term from its x (a column's edge), one from its
   z (a row's edge) and one from its height; each term is made once a frame
   (per edge) or once a tower (per height), in 16ths of a unit (16-bit:
   towers are within RANGE of the camera). One rounding where the engine
   has two: a sixteenth of a unit at most. */
int16_t ex_x[2 * DC_N], ex_y[2 * DC_N], ex_z[2 * DC_N];          /* column edges (view_asm.asm too) */
int16_t ez_x[2 * DC_N], ez_y[2 * DC_N], ez_z[2 * DC_N];          /* row edges */

static int16_t term(int16_t d, int16_t k) { return (int16_t)(mul16s(d, k) >> 8); }

static void edge_terms(void)
{
    /* rx = dx c - dz s; y = dy cp + (dx s + dz c) sp; z = (dx s + dz c) cp - dy sp */
    k_xc = cy12;                                    /* dx -> x */
    k_ys = (int16_t)(mul16s(sy12, sp12) >> 12);     /* dx -> y */
    k_zs = (int16_t)(mul16s(sy12, cp12) >> 12);     /* dx -> z */
    k_xs = -sy12;                                   /* dz -> x */
    k_yc = (int16_t)(mul16s(cy12, sp12) >> 12);     /* dz -> y */
    k_zc = (int16_t)(mul16s(cy12, cp12) >> 12);     /* dz -> z */
}

/* the terms of edges k0..k1 (2 per block) along one axis: products made by
   adding (exactly: 32-bit), taken in 16ths (>> 8) */
static void edge_run(int16_t first, uint8_t k0, uint8_t k1, int16_t ka, int16_t kb, int16_t kc,
                     int16_t *ta, int16_t *tb, int16_t *tc)
{
    int32_t pa = mul16s(first, ka), pb = mul16s(first, kb), pc = mul16s(first, kc);
    int32_t fa = mul16s(2 * DC_FOOT, ka), fb = mul16s(2 * DC_FOOT, kb), fc = mul16s(2 * DC_FOOT, kc);
    int32_t ga = mul16s(DC_PITCH - 2 * DC_FOOT, ka), gb = mul16s(DC_PITCH - 2 * DC_FOOT, kb),
            gc = mul16s(DC_PITCH - 2 * DC_FOOT, kc);
    uint8_t k;
    for (k = 2 * k0; k <= 2 * k1 + 1; k++) {
        ta[k] = (int16_t)(pa >> 8);
        tb[k] = (int16_t)(pb >> 8);
        tc[k] = (int16_t)(pc >> 8);
        if (k & 1) { pa += ga; pb += gb; pc += gc; }     /* to the next block's near edge */
        else { pa += fa; pb += fb; pc += fc; }           /* across the tower */
    }
}

static void col_terms(uint8_t k0, uint8_t k1)
{
    edge_run(-DC_HALF + k0 * DC_PITCH + DC_PITCH / 2 - DC_FOOT - camx, k0, k1, k_xc, k_ys, k_zs, ex_x, ex_y, ex_z);
}

static void row_terms(uint8_t k0, uint8_t k1)
{
    edge_run(-DC_HALF + k0 * DC_PITCH + DC_PITCH / 2 - DC_FOOT - camz, k0, k1, k_xs, k_yc, k_zc, ez_x, ez_y, ez_z);
}

int16_t g0_y, g0_z;                                 /* the ground's height terms */
extern void tower_pts(uint8_t c, uint8_t r, int16_t hy, int16_t hz);   /* view_asm.asm */

/* a tower: its roof and the (at most two) walls facing the camera; pv[0..3]
   the roof's corners (x0 z0, x1 z0, x1 z1, x0 z1), pv[4..7] the ground's */
static void tower(uint8_t c, uint8_t r, int16_t h, uint8_t tone)
{
    int16_t x0 = -DC_HALF + c * DC_PITCH + DC_PITCH / 2 - DC_FOOT, z0 = -DC_HALF + r * DC_PITCH + DC_PITCH / 2 - DC_FOOT;
    int16_t x1 = x0 + 2 * DC_FOOT, z1 = z0 + 2 * DC_FOOT;
    uint8_t lit = PEN_TOWER + 2 * tone, sh = lit + 1;
    tower_pts(c, r, term(h - camy, cp12), -term(h - camy, sp12));
    quad(0, 1, 2, 3, 0, PEN_ROOF);
    if (camz < z0) quad(4, 5, 1, 0, 0, lit);
    else if (camz > z1) quad(7, 6, 2, 3, 0, lit);
    if (camx < x0) quad(4, 7, 3, 0, 0, sh);
    else if (camx > x1) quad(5, 6, 2, 1, 0, sh);
}

static int16_t col_ahead[DC_N], row_ahead[DC_N];
static uint8_t col_in[DC_N];
static const uint8_t tone3[DC_N] = { 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2 };

static void city(const DCGame *g)
{
    int16_t c0 = (camx - RANGE + DC_HALF) / DC_PITCH, c1 = (camx + RANGE + DC_HALF) / DC_PITCH;
    int16_t r0 = (camz - RANGE + DC_HALF) / DC_PITCH, r1 = (camz + RANGE + DC_HALF) / DC_PITCH;
    int16_t r, c, goal = goal_of(g);
    /* (C's division truncates: a camera west of the city by more than the
       range gives c1 < 0 and nothing is drawn, as in the original) */
    if (camx - RANGE + DC_HALF < 0) c0 = 0;
    if (camz - RANGE + DC_HALF < 0) r0 = 0;
    if (c1 >= DC_N) c1 = DC_N - 1;
    if (r1 >= DC_N) r1 = DC_N - 1;
    view_towers = 0;
    if (c0 > c1 || r0 > r1) return;
    col_terms((uint8_t)c0, (uint8_t)c1);
    row_terms((uint8_t)r0, (uint8_t)r1);
    g0_y = term(-camy, cp12);
    g0_z = -term(-camy, sp12);
    /* render.c's cull, (dx fx + dz fz) >> 16, as a column's and a row's part */
    for (c = c0; c <= c1; c++) col_ahead[c] = (int16_t)(mul16s(-DC_HALF + c * DC_PITCH + DC_PITCH / 2 - camx, sy12) >> 12);
    for (r = r0; r <= r1; r++) row_ahead[r] = (int16_t)(mul16s(-DC_HALF + r * DC_PITCH + DC_PITCH / 2 - camz, cy12) >> 12);
    /* port: the per-cell tests split into per-column and per-row parts */
    for (c = c0; c <= c1; c++) {
        int16_t dx = -DC_HALF + c * DC_PITCH + DC_PITCH / 2 - camx;
        col_in[c] = dx <= RANGE && dx >= -RANGE;
    }
    {
        int16_t dr = goal / DC_N, dcol = goal % DC_N, pr = g->depot / DC_N, pc = g->depot % DC_N;
        for (r = r0; r <= r1; r++) {
            int16_t cz = -DC_HALF + r * DC_PITCH + DC_PITCH / 2, dz = cz - camz, ra = row_ahead[r];
            const int16_t *hrow = g->height[r];
            if (dz > RANGE || dz < -RANGE) continue;
            for (c = c0; c <= c1; c++) {
                int16_t h, cx;
                /* behind the camera, or out of range: skip it */
                if (col_ahead[c] + ra < -DC_PITCH || !col_in[c]) continue;
                h = hrow[c];
                cx = -DC_HALF + c * DC_PITCH + DC_PITCH / 2;
                if (h == 0) {
                    xform(cx, 1, cz, &pbase);
                    aflat(DC_FOOT, DC_FOOT, 0, 60, PEN_PARK);
                    continue;
                }
                tower((uint8_t)c, (uint8_t)r, h, tone3[r]);   /* (r * 7 + c * 3) % 3 */
                view_towers++;
                if ((r == pr && c == pc) || (r == dr && c == dcol)) {
                    int16_t rad = g->pad_r;
                    uint8_t pen = r == pr && c == pc ? PEN_DEPOT : PEN_CUST;
                    xform(cx, h + 1, cz, &pbase);
                    aflat(rad, rad, 0, -4, pen);
                    aflat(rad - 6, rad - 6, 1, -5, PEN_ROOF);
                    aflat(rad - 12, rad - 12, 2, -6, pen);
                }
            }
        }
    }
}

/* the beacon over the next pad: seen from anywhere ahead */
static void beacon(const DCGame *g)
{
    int16_t b = goal_of(g), bc = b % DC_N, br = b / DC_N;
    int16_t cx = -DC_HALF + bc * DC_PITCH + DC_PITCH / 2, cz = -DC_HALF + br * DC_PITCH + DC_PITCH / 2;
    int16_t h = g->height[br][bc], w = 3 + (int16_t)(g->tick & 2);
    wquad(cx - w, h, cz, cx + w, h, cz, cx + w, h + 260, cz, cx - w, h + 260, cz, -20, g->carrying ? PEN_CUST : PEN_DEPOT);
    wquad(cx, h, cz - w, cx, h, cz + w, cx, h + 260, cz + w, cx, h + 260, cz - w, -20, PEN_BEACON);
}

/* ------------------------------------------------------------ the drone */

/* port: the drone is hardware sprites (tools/gen_drone.py: render.c's model
   pre-rendered for each camera pitch, heading against the camera, parcel
   and rotor phase); here its frame and place, shown when this picture is
   (game.c, after the flip). Its shadow is still a polygon among the city's. */
DroneSpr view_drone;

static void drone(const DCGame *g, uint16_t pitch)
{
    int16_t x = (int16_t)(g->x >> 16), y = (int16_t)(g->y >> 16), z = (int16_t)(g->z >> 16);
    int16_t gy = ground_at(g, g->x, g->z);
    uint16_t t;
    uint8_t pk, h;
    /* the shadow, straight down (the city's axes) */
    xform(x, gy + 1, z, &pbase);
    aflat(14, 14, 0, 30, PEN_SHADOW);
    /* the drone's middle on the display (sprites: full rows, the border too) */
    xform(x, y, z, &pbase);
    view_drone.show = pbase.z >= NEARZ;
    if (!view_drone.show) return;
    view_drone.x = 32 + VCX + (int16_t)((int32_t)pbase.x * FOV / pbase.z) - DRONE_CX;
    view_drone.y = 32 + 2 * VCY - (int16_t)((int32_t)pbase.y * FOV / pbase.z) - DRONE_CY;
    /* the frame: the nearest pitch step, the heading against the camera's
       (180 degrees round is the same picture, -a the mirror of a) */
    pk = pitch < 700 ? 0 : (uint8_t)((pitch - 700) / 1000);
    if (pk >= DRONE_PITCHES) pk = DRONE_PITCHES - 1;
    t = ((uint16_t)g->yaw - view_cam_yaw) & 0x7FFF;
    view_drone.mirror = t > 16384;
    if (view_drone.mirror) t = 32768U - t;
    h = (uint8_t)((t + 2048) >> 12);
    view_drone.frame = (uint8_t)(((pk * DRONE_HEADINGS + h) * 2 + (g->carrying != 0)) * 2 + (uint8_t)(g->tick & 1));
    view_drone.fragile = g->fragile != 0;
}

/* ------------------------------------------------------------ the frame */

static void sky_ground(int16_t hz)
{
    /* plat_sky(4, 0x48C, 0xCDE, 120): 16 steps over the original's rows
       0..119 (here 3 rows each), its last colour below */
    uint8_t k;
    int16_t y = 0;
    for (k = 0; k < 16 && y < hz; k++, y += 3) l2p_rect(y, k == 15 ? hz : (y + 3 < hz ? y + 3 : hz), 0, VW - 1, PAL_SKY + k);
    l2p_rect(hz, VIEW_ROWS, 0, VW - 1, PEN_GROUND);
}

void view_scene(const DCGame *g, uint16_t cam_yaw)
{
    int32_t fx = a3_sin(cam_yaw), fz = a3_cos(cam_yaw);
    int16_t gy, hz;
    uint16_t pitch;
    clk_t t0;
    uint8_t b, k;
    clk_mark(&t0);
    camx = (int16_t)(g->x >> 16) - (int16_t)((fx * 170) >> 16);
    camz = (int16_t)(g->z >> 16) - (int16_t)((fz * 170) >> 16);
    camy = (int16_t)(g->y >> 16) + 70;
    gy = ground_at(g, (fix)camx << 16, (fix)camz << 16) + 20;
    if (camy < gy) camy = gy;
    /* looking down at the ground under the drone (a little beyond it), so
     * the roof it is over stays in view however high it flies */
    pitch = a3_atan2(camy - ground_at(g, g->x, g->z), 300);
    if (pitch < 1200) pitch = 1200;
    if (pitch > 9000) pitch = 9000;
    cy12 = (int16_t)(fz >> 4);
    sy12 = (int16_t)(fx >> 4);
    cp12 = (int16_t)(a3_cos(pitch) >> 4);
    sp12 = (int16_t)(a3_sin(pitch) >> 4);
    view_cam_yaw = cam_yaw;
    /* cam_horizon(), in the drawn rows */
    hz = VCY - (int16_t)((mul16s(FOV / 2, sp12)) / cp12);
    if (hz < 1) hz = 1;
    if (hz > VIEW_ROWS - 1) hz = VIEW_ROWS - 1;

    npool = 0;
    memset(bhead, NIL, NB);
    edge_terms();
    city_axes();
    ZXN_WRITE_MMU3(RECIP_PAGE);                  /* (the caller's page 44 comes back below) */
    city(g);
    beacon(g);
    drone(g, pitch);
    ZXN_WRITE_MMU3(44);
    view_polys = npool;
    view_t_build += clk_lines(&t0);
    clk_mark(&t0);

    l2p_begin();
    sky_ground(hz);
    for (b = NB - 1;; b--) {
        for (k = bhead[b]; k != NIL; k = pool[k].next) {
            if (pool[k].n & 0x80) fastpoly(&pool[k]);        /* wholly on the screen (view_asm.asm) */
            else l2p_poly(pool[k].xy, pool[k].n, pool[k].col);
        }
        if (!b) break;
    }
    l2p_end();
    view_t_fill += clk_lines(&t0);
}

void view_blank(void)
{
    view_drone.show = 0;
    l2p_begin();
    l2p_rect(0, VIEW_ROWS, 0, VW - 1, 0);
    l2p_end();
}

void view_init(void)
{
    pal_load(1, gfx_palette, 48);
    l2p_top = 0;
    l2p_bot = VIEW_ROWS;
    l2p_xmin = 0;
    l2p_xmax = 255;
    persp_fov(FOV);
    copper_begin();
    l2dbl_copper();                          /* port: rows 0..95 shown twice each (l2double.h) */
    copper_end();
}
