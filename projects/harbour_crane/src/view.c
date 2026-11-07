/*
 * The picture (the original's render.c scene()): the quay with its
 * containers, the gantry crane with its trolley, cable and hook, the ship
 * (rolled by her heel and lifted by the swell) and her load, the container
 * on the hook (swung with the cable) or on its way down, and the splash.
 *
 * Each frame the back buffer (Layer 2, 256 x 192, double buffered) gets
 * the level backdrop by DMA, then this frame's polygons, filled by l2poly
 * in the original's order: a3d_draw.c's 512 depth buckets, far to near,
 * in the order they were added within a bucket.
 *
 * port: three layers instead of one redrawn scene:
 *   - the backdrop (tools/gen_gfx.py, banks 16-18): sky, shore, sea, quay;
 *     the sea's bands drift by palette (sea_palette)
 *   - the level backdrop (banks 27-29): the backdrop plus what only changes
 *     when a container leaves the quay - the queue behind the pick spot and
 *     the crane's far side (legs, rail, machinery house), drawn in the
 *     original's order when it changes (view_rebuild)
 *   - every frame: the ship and her load, the crane's near side, crossbeams
 *     and trolley, the cable and hook, the loads, the splash
 * This file is in bank 21, mapped at 0x0000 while it works (mem.h VIEW()),
 * with its lists; whole screens are copied by bank_copy (cv.asm), which
 * goes through slots 2 and 3, not 0 and 1 as the SDK's dma_copy_banks.
 * The camera is fixed (tools/geom.py) and the view is the original's at
 * 0.8. Points are worked out in camera space with Q12 maps and a
 * reciprocal table (cv.asm): positions can be a pixel off the original's.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "hc.h"
#include "view.h"
#include "gfx_data.h"
#include "nextgfx.h"
#include "l2poly.h"
#include "palfx.h"
#include "fixmath.h"
#include "mem.h"
#include <z80.h>

#define SPR_CRANE 0                         /* sprites 0..CRANE_SPRITES-1 */

#define LEVEL_BANK 27                       /* the level backdrop: banks 27-29 */

/* the original's pens (render.c) as Layer 2 colours */
#define C(p)       (PEN0 + (p))
#define PEN_QUAY   8
#define PEN_CRANE  10
#define PEN_HULL   12
#define PEN_DECK   14
#define PEN_BRIDGE 15
#define PEN_CABLE  16
#define PEN_BOX    17
#define PEN_WINDOW 25
#define PEN_YELLOW 27
#define PEN_FOAM   31
#define BOX_Z      20

/* ---------------------------------------------------------- the draw list */
#define NPOLY 200
static int16_t poly_xy[NPOLY][10];
static uint8_t poly_n[NPOLY], poly_col[NPOLY], poly_next[NPOLY];
static uint8_t bhead[512], btail[512];      /* 0xFF: empty */
static uint8_t npoly;
static uint16_t b_lo, b_hi;                 /* the buckets used */
uint8_t view_polys, view_dropped;          /* (in this bank: read them while it is mapped) */

static void list_begin(void)
{
    if (b_hi >= b_lo) memset(bhead + b_lo, 0xFF, b_hi - b_lo + 1);
    b_lo = 511;
    b_hi = 0;
    npoly = 0;
}

/* a3d_draw.c bucket_of with a3_begin(2000): depth * 512 / 2000, exactly */
static uint16_t bucket(int16_t depth)
{
    if (depth <= 0) return 0;
    if (depth >= 2000) return 511;
    /* (d * 67109) >> 18, which is d * 512 / 2000 for every d < 4000:
       floor((d + floor(d * 1573 / 65536)) / 4) */
    return ((uint16_t)depth + (uint16_t)(mulu16((uint16_t)depth, 1573) >> 16)) >> 2;
}

static void add(const int16_t *xy, uint8_t n, int16_t depth, uint8_t col)
{
    uint16_t b;
    uint8_t k = npoly;
    if (k >= NPOLY) {
        view_dropped++;
        return;
    }
    memcpy(poly_xy[k], xy, n * 4);
    poly_n[k] = n;
    poly_col[k] = col;
    poly_next[k] = 0xFF;
    b = bucket(depth);
    if (b < b_lo) b_lo = b;
    if (b > b_hi) b_hi = b;
    if (bhead[b] == 0xFF) bhead[b] = k;
    else poly_next[btail[b]] = k;
    btail[b] = k;
    npoly++;
}

static void list_draw(void)
{
    uint16_t b = b_hi + 1;
    uint8_t k;
    l2p_begin();
    while (b-- > b_lo)
        for (k = bhead[b]; k != 0xFF; k = poly_next[k]) l2p_poly(poly_xy[k], poly_n[k], poly_col[k]);
    l2p_end();
    view_polys = npoly;
}

/* ---------------------------------------------------------- points */
/* A point in the fixed camera's space: X, Y, Z in 1/16 units (cv.asm).
   How a part of the scene is placed (the original's xf_*: the world; the
   ship, rolled about the pivot by heel and lifted by bob; the load, swung
   about the hook by th) and the camera together are an affine map:
   camera = T + (x Vx + y Vy + z Vz) >> 8, Vx, Vy, Vz the map's columns in
   Q12. A box's corners are its first corner plus its edges' vectors (adds),
   so a box costs a few multiplies and a projection a corner. */
typedef int16_t CV[3];
extern void cv_mul(int16_t s, const int16_t *v, int16_t *out);
extern void cv_proj(const int16_t *p, int16_t *xy);
extern void bank_copy(uint16_t dst, uint16_t src);   /* a 48K screen: 3 banks by DMA (cv.asm) */
extern int16_t cv_rcp[1024];                /* fov * 4096 / z for z = 2 i (cv.asm) */

static int16_t Vx[3], Vy[3], Vz[3], T[3];

#define CV_ADD(o, a, b) do { (o)[0] = (a)[0] + (b)[0]; (o)[1] = (a)[1] + (b)[1]; (o)[2] = (a)[2] + (b)[2]; } while (0)
#define CV_SUB(o, a, b) do { (o)[0] = (a)[0] - (b)[0]; (o)[1] = (a)[1] - (b)[1]; (o)[2] = (a)[2] - (b)[2]; } while (0)

/* the placement: rotation R (Q12, rows (r00 r01) (r10 r11)) about (px, py),
   then lifted by bob; mode 0 the world */
static void xf_set(int16_t r00, int16_t r01, int16_t r10, int16_t r11, int16_t px, int16_t py, int16_t bob)
{
    int32_t wx0, wy0;
    Vx[0] = r00;
    Vx[1] = (int16_t)(mul16(r10, CAM_CP12) >> 12);
    Vx[2] = (int16_t)(-mul16(r10, CAM_SP12) >> 12);
    Vy[0] = r01;
    Vy[1] = (int16_t)(mul16(r11, CAM_CP12) >> 12);
    Vy[2] = (int16_t)(-mul16(r11, CAM_SP12) >> 12);
    Vz[0] = 0;
    Vz[1] = CAM_SP12;
    Vz[2] = CAM_CP12;
    /* where the part's origin goes (world, 1/4096 units), then the camera */
    wx0 = ((int32_t)px << 12) - mul16(r00, px) - mul16(r01, py);
    wy0 = ((int32_t)(py + bob) << 12) - mul16(r10, px) - mul16(r11, py);
    wx0 -= (int32_t)CAM_X << 12;
    wy0 -= (int32_t)CAM_Y << 12;
    T[0] = (int16_t)(wx0 >> 8);
    T[1] = (int16_t)(((wy0 >> 8) * CAM_CP12 + (int32_t)(-CAM_Z) * 16 * CAM_SP12) >> 12);
    T[2] = (int16_t)(((int32_t)(-CAM_Z) * 16 * CAM_CP12 - (wy0 >> 8) * CAM_SP12) >> 12);
}

/* the world's placement doesn't change: worked out once (view_init) */
static int16_t w_Vx[3], w_Vy[3], w_T[3];

static void xf_world(void)
{
    memcpy(Vx, w_Vx, sizeof(Vx));
    memcpy(Vy, w_Vy, sizeof(Vy));
    memcpy(T, w_T, sizeof(T));
}

static void xf_world_init(void)
{
    xf_set(4096, 0, 0, 4096, 0, 0, 0);
    memcpy(w_Vx, Vx, sizeof(Vx));
    memcpy(w_Vy, Vy, sizeof(Vy));
    memcpy(w_T, T, sizeof(T));
}

/* mode 1 the ship (heel: the right side down), 2 the load (th: the bottom
   swings to +x) */
static void xf_rot(uint8_t mode, fix angle, int16_t px, int16_t py, int16_t bob)
{
    uint16_t a = (uint16_t)a3_angle(angle);
    int16_t c = (int16_t)(a3_cos(a) >> 4), s = (int16_t)(a3_sin(a) >> 4);     /* Q12 */
    if (mode == 1) xf_set(c, s, -s, c, px, py, bob);
    else xf_set(c, -s, s, c, px, py, bob);
}

/* z's part, z Vz: Vz is the camera's own (no placement turns z), so the
   few z values the scene uses are worked out once */
#define ZCACHE 16
static int16_t zc_key[ZCACHE], zc_val[ZCACHE][3];
static uint8_t zc_n;

static const int16_t *zpart(int16_t z)
{
    uint8_t k;
    for (k = 0; k < zc_n; k++)
        if (zc_key[k] == z) return zc_val[k];
    if (zc_n < ZCACHE) {
        zc_key[zc_n] = z;
        cv_mul(z, Vz, zc_val[zc_n]);
        return zc_val[zc_n++];
    }
    cv_mul(z, Vz, zc_val[ZCACHE - 1]);      /* (never: the scene has fewer) */
    zc_key[ZCACHE - 1] = z;
    return zc_val[ZCACHE - 1];
}

/* a point of the part */
static void at(int16_t x, int16_t y, int16_t z, int16_t *o)
{
    static CV t;
    const int16_t *zp = zpart(z);
    cv_mul(x, Vx, o);
    cv_mul(y, Vy, t);
    o[0] += t[0] + zp[0] + T[0];
    o[1] += t[1] + zp[1] + T[1];
    o[2] += t[2] + zp[2] + T[2];
}

static int16_t q[10];

/* a face from camera points: the original's depth is their mean z + bias */
static void face(const int16_t *a, const int16_t *b, const int16_t *c, const int16_t *d, int16_t bias, uint8_t col)
{
    cv_proj(a, q);
    cv_proj(b, q + 2);
    cv_proj(c, q + 4);
    cv_proj(d, q + 6);
    add(q, 4, (((a[2] >> 4) + (b[2] >> 4) + (c[2] >> 4) + (d[2] >> 4)) >> 2) + bias, col);
}

static void quad(int16_t x0, int16_t y0, int16_t z0, int16_t x1, int16_t y1, int16_t z1,
                 int16_t x2, int16_t y2, int16_t z2, int16_t x3, int16_t y3, int16_t z3, int16_t bias, uint8_t col)
{
    static CV a, b, c, d;
    at(x0, y0, z0, a);
    at(x1, y1, z1, b);
    at(x2, y2, z2, c);
    at(x3, y3, z3, d);
    face(a, b, c, d, bias, col);
}

/* a box: its front, its top, and the end facing the camera. Its corners
   (bk: 0 = x0 y0 z0, 1 = x1 y0 z0, 2 = x1 y1 z0, 3 = x0 y1 z0, 4 = x0 y1 z1,
   5 = x1 y1 z1, 6 = the end's fourth) are projected once each */
static CV bk[7];
static int16_t bxy[8][2];

static void face_k(uint8_t a, uint8_t b, uint8_t c, uint8_t d, int16_t bias, uint8_t col)
{
    q[0] = bxy[a][0]; q[1] = bxy[a][1];
    q[2] = bxy[b][0]; q[3] = bxy[b][1];
    q[4] = bxy[c][0]; q[5] = bxy[c][1];
    q[6] = bxy[d][0]; q[7] = bxy[d][1];
    add(q, 4, (((bk[a][2] >> 4) + (bk[b][2] >> 4) + (bk[c][2] >> 4) + (bk[d][2] >> 4)) >> 2) + bias, col);
}

/* the box from its first corner (bk[0], set) and its edges' vectors */
static void box_faces(const int16_t *W, const int16_t *H, const int16_t *D, uint8_t end, uint8_t lit, uint8_t shade,
                      int16_t bias)
{
    uint8_t k, n = end ? 7 : 6;
    CV_ADD(bk[1], bk[0], W);
    CV_ADD(bk[2], bk[1], H);
    CV_ADD(bk[3], bk[0], H);
    CV_ADD(bk[4], bk[3], D);
    CV_ADD(bk[5], bk[2], D);
    if (end == 1) CV_ADD(bk[6], bk[1], D);
    else if (end == 2) CV_ADD(bk[6], bk[0], D);
    for (k = 0; k < n; k++) cv_proj(bk[k], bxy[k]);
    face_k(0, 1, 2, 3, bias, lit);
    face_k(3, 2, 5, 4, bias + 1, shade);
    if (end == 1) face_k(1, 2, 5, 6, bias + 1, shade);
    else if (end == 2) face_k(0, 3, 4, 6, bias + 1, shade);
}

/* which end the camera sees: 1 the x1 end, 2 the x0 end, 0 neither */
#define END_OF(x0, x1) ((x1) < CAM_X ? 1 : (x0) > CAM_X ? 2 : 0)

static CV kW, kH, kD;

static void box(int16_t x0, int16_t x1, int16_t y0, int16_t y1, int16_t z0, int16_t z1, uint8_t lit, uint8_t shade,
                int16_t bias)
{
    at(x0, y0, z0, bk[0]);
    cv_mul(x1 - x0, Vx, kW);
    cv_mul(y1 - y0, Vy, kH);
    cv_mul(z1 - z0, Vz, kD);
    box_faces(kW, kH, kD, END_OF(x0, x1), lit, shade, bias);
}

/* a container whose box is in bk/bxy: the front's inset (2 units in from
   the ends, 3 from the top and bottom, a unit nearer) and the top's (2 in
   from the ends) in the lit colour, leaving the darker bands.
   port: the insets are made on the screen (they are under a pixel or two
   at this scale): the bands are a pixel, the ends a pixel on boxes over
   24 pixels wide */
static void container_insets(uint8_t lit, int16_t bias)
{
    int16_t ix = bxy[1][0] - bxy[0][0] > 24 ? 1 : 0;
    q[0] = bxy[0][0] + ix; q[1] = bxy[0][1] - 1;
    q[2] = bxy[1][0] - ix; q[3] = bxy[1][1] - 1;
    q[4] = bxy[2][0] - ix; q[5] = bxy[2][1] + 1;
    q[6] = bxy[3][0] + ix; q[7] = bxy[3][1] + 1;
    add(q, 4, (((bk[0][2] >> 4) + (bk[1][2] >> 4) + (bk[2][2] >> 4) + (bk[3][2] >> 4)) >> 2) + bias - 1, lit);
    q[0] = bxy[3][0] + ix; q[1] = bxy[3][1];
    q[2] = bxy[2][0] - ix; q[3] = bxy[2][1];
    q[4] = bxy[5][0] - ix; q[5] = bxy[5][1];
    q[6] = bxy[4][0] + ix; q[7] = bxy[4][1];
    add(q, 4, (((bk[3][2] >> 4) + (bk[2][2] >> 4) + (bk[5][2] >> 4) + (bk[4][2] >> 4)) >> 2) + bias, lit);
}

/* a container anywhere: coloured by weight (lit and shaded pens) */
static void container(int16_t x0, int16_t y0, int16_t w, uint8_t weight, int16_t bias)
{
    uint8_t lit = C(PEN_BOX) + 2 * (weight - 1);
    box(x0, x0 + w, y0, y0 + HC_BOX_H, -BOX_Z, BOX_Z, lit + 1, lit + 1, bias);
    container_insets(lit, bias);
}

/* ---------------------------------------------------------- the scene's parts */
/* the rules' hc_box_w, hc_hook_x, hc_hook_y (the same arithmetic): the
   rules' bank is not mapped while this one draws */
static int16_t box_w(const HCGame *g, int b) { return g->box[b].len * HC_SLOT_W - 2; }
static fix hook_x(const HCGame *g) { return g->tx + fmul(g->L, fsin(g->th)); }
static fix hook_y(const HCGame *g) { return FROMI(HC_GANTRY_Y) - fmul(g->L, fcos(g->th)); }

static const int16_t legs[2] = {-300, -40};
#define TOP HC_GANTRY_Y

/* the quay's queue (the original's quay(), behind the pick spot) and the
   crane's far side: drawn into the level backdrop */
static void queue_and_far(const HCGame *g)
{
    uint8_t k;
    xf_world();
    for (k = 1; k <= 4 && g->next + k < g->nbox; k++) {
        const HCBox *b = &g->box[g->next + k];
        int16_t w = b->len * HC_SLOT_W - 2, x0 = HC_PICK_X - 60 - k * 70 - w / 2;
        uint8_t lit = C(PEN_BOX) + 2 * (b->weight - 1);
        box(x0, x0 + w, 0, HC_BOX_H, 120, 160, lit, lit + 1, 0);
    }
    for (k = 0; k < 2; k++)
        box(legs[k] - 8, legs[k] + 8, 0, TOP + 25, 40, 52, C(PEN_CRANE), C(PEN_CRANE + 1), 0);
    box(-320, 460, TOP + 10, TOP + 25, 40, 52, C(PEN_CRANE), C(PEN_CRANE + 1), 0);              /* the far rail */
    box(-330, -280, TOP + 25, TOP + 70, -30, 30, C(PEN_CRANE + 1), C(PEN_CRANE + 1), 0);        /* the machinery house */
}

/* the next container, on the pick spot */
static void pick(const HCGame *g)
{
    if (g->carrying < 0 && g->falling < 0 && g->next < g->nbox) {
        int16_t w = box_w(g, g->next);
        container(HC_PICK_X - w / 2, 0, w, g->box[g->next].weight, 0);
    }
}

static void crane(const HCGame *g)
{
    int16_t tx = (int16_t)(g->tx >> 16), hx = (int16_t)(hook_x(g) >> 16), hy = (int16_t)(hook_y(g) >> 16);
    uint8_t k;
    xf_world();
    /* port: the near legs, the crossbeams, the near rail and the brace are
       sprites (view_init), over everything: the original draws them before
       the trolley, so there the trolley and the cable's top cover the rail */
    box(tx - 18, tx + 18, TOP, TOP + 14, -48, 48, C(PEN_CRANE + 1), C(PEN_CRANE + 1), -2);       /* the trolley */
    {                                                                                       /* the cable */
        static CV a, b, c, d;
        at(tx - 2, TOP, -2, a);
        at(tx + 2, TOP, -2, b);
        at(hx + 2, hy, -2, c);
        at(hx - 2, hy, -2, d);
        cv_proj(a, q);
        cv_proj(b, q + 2);
        cv_proj(c, q + 4);
        cv_proj(d, q + 6);
        add(q, 4, (int16_t)((((int32_t)a[2] + c[2]) >> 1) >> 4) - 25, C(PEN_CABLE));
    }
    if (g->carrying < 0) box(hx - 6, hx + 6, hy - 4, hy + 4, -6, 6, C(PEN_CABLE), C(PEN_CABLE), -26);
}

static void ship(const HCGame *g)
{
    uint8_t k;
    static CV a;
    int16_t d;
    xf_rot(1, g->heel, HC_PIVOT_X, HC_PIVOT_Y, (int16_t)(g->bob >> 16));
    /* the hull: its side, with a bow, and the deck */
    at(HC_HULL_X0 - 25, HC_DECK_Y + 6, -60, a);
    cv_proj(a, q);
    d = a[2] >> 4;
    at(HC_HULL_X1 + 10, HC_DECK_Y + 6, -60, a);
    cv_proj(a, q + 2);
    at(HC_HULL_X1 - 5, HC_WATER_Y - 30, -60, a);
    cv_proj(a, q + 4);
    at(HC_HULL_X0 + 15, HC_WATER_Y - 30, -60, a);
    cv_proj(a, q + 6);
    at(HC_HULL_X0 - 10, HC_WATER_Y + 10, -60, a);
    cv_proj(a, q + 8);
    add(q, 5, d + 20, C(PEN_HULL));
    quad(HC_HULL_X0 - 25, HC_DECK_Y + 6, -60, HC_HULL_X1 + 10, HC_DECK_Y + 6, -60, HC_HULL_X1 + 10, HC_DECK_Y, -60,
         HC_HULL_X0 - 25, HC_DECK_Y, -60, 18, C(PEN_FOAM));
    quad(HC_HULL_X0 - 25, HC_DECK_Y, -60, HC_HULL_X1 + 10, HC_DECK_Y, -60, HC_HULL_X1 + 10, HC_DECK_Y, 60,
         HC_HULL_X0 - 25, HC_DECK_Y, 60, 40, C(PEN_DECK));
    {                                          /* the slots' edges on the deck */
        static CV p0, w2, d60, step, p1, p2, p3;
        at(HC_SHIP_X0 - 1, HC_DECK_Y + 1, -30, p0);
        cv_mul(2, Vx, w2);
        cv_mul(60, Vz, d60);
        cv_mul(HC_SLOT_W, Vx, step);
        for (k = 0; k <= HC_SLOTS; k++) {
            CV_ADD(p1, p0, w2);
            CV_ADD(p2, p1, d60);
            CV_ADD(p3, p0, d60);
            face(p0, p1, p2, p3, 38, C(PEN_YELLOW));
            CV_ADD(p0, p0, step);
        }
    }
    /* the bridge at the stern */
    box(HC_SHIP_X0 + HC_SLOTS * HC_SLOT_W + 6, HC_HULL_X1 + 2, HC_DECK_Y, HC_DECK_Y + 80, -40, 40, C(PEN_BRIDGE),
        C(PEN_BRIDGE - 12), 0);
    box(HC_SHIP_X0 + HC_SLOTS * HC_SLOT_W + 10, HC_HULL_X1 - 2, HC_DECK_Y + 58, HC_DECK_Y + 70, -41, -40,
        C(PEN_WINDOW), C(PEN_WINDOW), -1);
    box(HC_SHIP_X0 + HC_SLOTS * HC_SLOT_W + 4, HC_HULL_X1 + 6, HC_DECK_Y + 80, HC_DECK_Y + 86, -44, 44, C(PEN_HULL),
        C(PEN_HULL + 1), 0);
    if (g->nplaced) {                          /* the load: corners from the ship's lattice */
        static CV o, slot, tier, w1, w2, h, d, base;
        at(HC_SHIP_X0 + 1, HC_DECK_Y, -BOX_Z, o);
        cv_mul(HC_SLOT_W, Vx, slot);
        cv_mul(HC_BOX_H, Vy, tier);
        cv_mul(HC_SLOT_W - 2, Vx, w1);
        cv_mul(2 * HC_SLOT_W - 2, Vx, w2);
        cv_mul(2 * BOX_Z, Vz, d);
        for (k = 0; k < g->nplaced; k++) {
            const HCPlaced *p = &g->placed[k];
            uint8_t j, lit = C(PEN_BOX) + 2 * (g->box[p->box].weight - 1);
            int16_t x0 = HC_SHIP_X0 + p->slot * HC_SLOT_W + 1;
            base[0] = o[0];
            base[1] = o[1];
            base[2] = o[2];
            for (j = 0; j < p->slot; j++) CV_ADD(base, base, slot);
            for (j = 0; j < p->tier; j++) CV_ADD(base, base, tier);
            bk[0][0] = base[0];
            bk[0][1] = base[1];
            bk[0][2] = base[2];
            box_faces(g->box[p->box].len == 2 ? w2 : w1, tier, d, END_OF(x0, x0 + box_w(g, p->box)), lit + 1,
                      lit + 1, -p->tier);
            container_insets(lit, -p->tier);
        }
    }
    xf_world();
}

static void load(const HCGame *g)
{
    if (g->carrying >= 0) {
        int16_t hx = (int16_t)(hook_x(g) >> 16), hy = (int16_t)(hook_y(g) >> 16);
        int16_t w = box_w(g, g->carrying);
        xf_rot(2, g->th, hx, hy, 0);
        box(hx - w / 2 - 4, hx + w / 2 + 4, hy, hy + 5, -24, 24, C(PEN_CABLE), C(PEN_CABLE), -30);   /* the spreader */
        container(hx - w / 2, hy - HC_BOX_H, w, g->box[g->carrying].weight, -30);
        xf_world();
    }
    if (g->falling >= 0)
        container((int16_t)(g->fx >> 16), (int16_t)(g->fy >> 16), box_w(g, g->falling), g->box[g->falling].weight,
                  -30);
}

/* ---------------------------------------------------------- the sea */
/* The original's sea: bands 300 deep, dark and light, at z + drift. Index
   i (0..119) is z = 5 i modulo 600 (tools/gen_gfx.py): it shows band
   floor((5 i - drift) / 300) & 1, so the 120 entries are a run of 60 dark
   and 60 light shifted by k = ceil(drift / 5): sea_run from 120 - k. The
   near indices (120..179, z = -400 + 5 j) show the sky until the bands'
   near edge, -400 + drift: near_run from 60 - k. Two DMA loads a tick. */
static uint8_t sea_run[240 * 2], near_run[120 * 2];
static int8_t sea_k = -1;

static void put_rgb(uint8_t *b, uint8_t n, uint16_t c)
{
    for (; n; n--) {
        *b++ = (uint8_t)(c >> 1);
        *b++ = c & 1;
    }
}

static void sea_init(void)
{
    put_rgb(sea_run, 60, sea_rgb[0]);
    put_rgb(sea_run + 120, 60, sea_rgb[1]);
    put_rgb(sea_run + 240, 60, sea_rgb[0]);
    put_rgb(sea_run + 360, 60, sea_rgb[1]);
    put_rgb(near_run, 60, sky_near_rgb);
    put_rgb(near_run + 120, 60, sea_rgb[0]);
    sea_k = -1;
}

static void sea_palette(const HCGame *g)
{
    int16_t drift = (int16_t)((g->phase >> 12) * 300 / 102);
    int8_t k = (int8_t)((drift + 4) / 5);
    if (k == sea_k) return;
    sea_k = k;
    palfx_load_bytes(PAL_L2, SEA_MOD, sea_run + (120 - k) * 2, 120 * 2);
    palfx_load_bytes(PAL_L2, SEA_NEAR, near_run + (60 - k) * 2, 60 * 2);
}

/* ---------------------------------------------------------- the frame */
static uint8_t q_next = 0xFF, q_nbox, q_level, q_sig;

static uint8_t queue_sig(const HCGame *g)
{
    uint8_t k, s = 0;
    for (k = 1; k <= 4 && g->next + k < g->nbox; k++) s = s * 7 + g->box[g->next + k].len * 4 + g->box[g->next + k].weight;
    return s;
}

void view_rebuild(const HCGame *g)
{
    uint8_t back = l2_back;
    bank_copy(LEVEL_BANK, BACK_BANK);
    l2_back = LEVEL_BANK;
    list_begin();
    queue_and_far(g);
    list_draw();
    l2_back = back;
    q_next = (uint8_t)g->next;
    q_nbox = (uint8_t)g->nbox;
    q_level = (uint8_t)g->level;
    q_sig = queue_sig(g);
}

/* the crane's near side: 4-bit sprites, fixed (tools/gen_gfx.py crane_near) */
static void crane_sprites(void)
{
    static uint8_t a[5];
    uint8_t k, p;
    int16_t x;
    pal_set(2, CRANE_SPR_PAL * 16 + 1, l2_pal[C(PEN_CRANE)]);
    pal_set(2, CRANE_SPR_PAL * 16 + 2, l2_pal[C(PEN_CRANE + 1)]);
    ZXN_WRITE_REG(0x4B, 0);                   /* sprites: index 0 (4-bit: pen 0) is clear */
    ZXN_WRITE_MMU2(CRANE_PAT_PAGE);
    z80_outp(0x303B, 0);
    dma_to_port(0x5B, (const void *)0x4000, CRANE_PATTERNS * 128);
    ZXN_WRITE_MMU2(10);
    z80_outp(0x303B, SPR_CRANE);
    for (k = 0; k < CRANE_SPRITES; k++) {
        x = crane_spr_x[k];
        p = crane_spr_p[k];
        a[0] = (uint8_t)x;
        a[1] = crane_spr_y[k];
        a[2] = (CRANE_SPR_PAL << 4) | ((x >> 8) & 1);
        a[3] = 0xC0 | (p >> 1);               /* visible, 5 bytes, pattern N5..N0 */
        a[4] = 0x80 | ((p & 1) << 6);         /* 4-bit, N6 */
        z80_otir(a, 0x57, 5);
    }
}

void view_init(void)
{
    zc_n = 0;                                   /* (this bank's variables aren't cleared at load) */
    view_polys = view_dropped = 0;
    memset(bhead, 0xFF, sizeof(bhead));
    b_lo = 1;
    b_hi = 0;
    uint16_t i;
    cv_rcp[0] = 32767;
    for (i = 1; i < 1024; i++) {
        uint32_t r = ((uint32_t)VIEW_FOV * 4096) / (i * 2);
        cv_rcp[i] = r > 32767 ? 32767 : (int16_t)r;
    }
    sea_init();
    xf_world_init();
    crane_sprites();
    palfx_load(PAL_L2, 0, 256, l2_pal);
    palfx_load(PAL_L2 | PAL_SECOND, 0, 256, l2_pal);
    q_next = 0xFF;
}

void view_draw(const HCGame *g, uint8_t splash_timer, int16_t splash_x)
{
    if (g->next != q_next || g->nbox != q_nbox || g->level != q_level || queue_sig(g) != q_sig) view_rebuild(g);
    sea_palette(g);
    bank_copy(l2_back, LEVEL_BANK);
    list_begin();
    pick(g);
    ship(g);
    crane(g);
    load(g);
    if (splash_timer) {
        int16_t x = splash_x, r = 6 + (20 - splash_timer) * 3;
        quad(x - r, HC_WATER_Y + 1, -r / 2, x + r, HC_WATER_Y + 1, -r / 2, x + r, HC_WATER_Y + 1, r / 2, x - r,
             HC_WATER_Y + 1, r / 2, -40, C(PEN_FOAM));
    }
    list_draw();
}
