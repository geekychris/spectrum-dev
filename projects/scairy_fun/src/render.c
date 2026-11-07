// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * render.c - ScAIry Fun's renderer on the Next: the road drawn as segments,
 * far to near, into the Layer 2 back buffer (port of the original's render.c).
 *
 * As in the original each 8 m segment has a cross-section of 8 points: two
 * on each side out in the scenery (levee slope and river, embankment and
 * field, rock wall, cliff and valley, pavement, canal), the shoulder edges
 * and the road edges. Its strips are drawn outer to inner, then the road,
 * the lines, rails, the autopilot's path, cones and scenery; a painter's
 * algorithm by segment gets hills and bends right without a depth sort.
 *
 * port: the camera is road-aligned pseudo-3D instead of a rotated 3D one:
 * every point of a cross-section shares its depth, so a segment edge costs
 * one division (persp_scale) and a MUL per coordinate (persp_xy); the turn
 * of the camera is a shear (tan(yaw) * depth) and the pitch a fixed horizon.
 * Polygons are filled by sdk l2poly (asm). Vehicles, animals and cones are
 * hardware sprites (sprites.c); the scenery is drawn here as the original's
 * boxes and pyramids, flat on to the camera.
 */
#include <string.h>
#include <stdint.h>

#include <arch/zxn.h>
#include "sc.h"
#include "render.h"
#include "sprites.h"
#include "l2poly.h"
#include "persp.h"

#define SEGF       ((int32_t)SEG_LEN * (1 << FBITS))
#define CAM_BACK   230                  /* chase camera: units behind the car */
#define CAM_UP     92
#define LINE_SEGS  16                   /* port: lane lines this far out (under half a pixel wide there) */
#define SCENE_SEGS 30
#define DRAW_SEGS  24
#define NEAR       20                   /* units: the near plane */

Cam cam;
uint16_t render_polys;
extern uint8_t render_skip;             /* profiling: bits skip parts of the frame (main.c) */

/* ---------- colours (the original's, 12-bit) ---------- */
const uint16_t route_palette[NUM_ROUTES][32] = {
    /* levee road: morning */
    { 0x000, 0xFFF, 0x333, 0x999, 0x8BF, 0x9AB, 0x357, 0x469,
      0x444, 0x555, 0x998, 0x4A3, 0x5B4, 0x763, 0x874, 0x776,
      0xE22, 0x811, 0xFE2, 0xF72, 0x2EF, 0x3F5, 0x36D, 0x128,
      0xEEE, 0xAAB, 0x774, 0x263, 0x124, 0x642, 0xB85, 0x111 },
    /* rush hour: hazy noon */
    { 0x000, 0xFFF, 0x333, 0x999, 0xABC, 0xAAA, 0x467, 0x578,
      0x555, 0x666, 0x999, 0x6A4, 0x7B5, 0x875, 0x986, 0x887,
      0xE22, 0x811, 0xFE2, 0xF72, 0x2EF, 0x3F5, 0x36D, 0x128,
      0xEEE, 0xAAB, 0x774, 0x373, 0x124, 0x642, 0xB85, 0x111 },
    /* mountain pass: sunset */
    { 0x000, 0xFFF, 0x333, 0x999, 0xF96, 0x869, 0x358, 0x469,
      0x434, 0x545, 0x987, 0x574, 0x685, 0x653, 0x764, 0x766,
      0xE22, 0x811, 0xFE2, 0xF72, 0x2EF, 0x3F5, 0x36D, 0x128,
      0xEEE, 0xAAB, 0x774, 0x243, 0x124, 0x642, 0xB85, 0x111 },
    /* flooded town: overcast */
    { 0x000, 0xFFF, 0x333, 0x999, 0x99A, 0x778, 0x456, 0x567,
      0x333, 0x444, 0x888, 0x585, 0x696, 0x665, 0x776, 0x666,
      0xE22, 0x811, 0xFE2, 0xF72, 0x2EF, 0x3F5, 0x36D, 0x128,
      0xEEE, 0xAAB, 0x774, 0x353, 0x124, 0x642, 0xB85, 0x111 },
};

const uint16_t route_sky[NUM_ROUTES][2] = {
    { 0x36C, 0xBDF }, { 0x79B, 0xDDC }, { 0x448, 0xFA5 }, { 0x667, 0xBBB }
};

/* side profiles: (out, dy) of the middle and far points beyond the shoulder */
static const int16_t side_prof[SIDE_COUNT][4] = {
    { 200,    -2, 1600,   -8 },         /* grass */
    { 150, -LEVEE_H, 2000, -LEVEE_H },  /* levee: slope down to the river */
    { 220,  -200, 1600, -210 },         /* drop: embankment down to a field */
    {  30,   180,  700,  260 },         /* rock wall */
    {  50,  -500, 1800, -520 },         /* cliff */
    {  40,     0, 1600,   -4 },         /* median */
    {  70,     3, 1600,    3 },         /* town pavement */
    {  42,   -60, 1800,  -60 },         /* canal */
};

/* tan(a) * 256 for a = 0, 256, ... 8192 (0..45 degrees) */
static const int16_t tan_tab[33] = {
    0, 6, 13, 19, 25, 31, 38, 44, 51, 57, 64, 71, 78, 85, 92, 99, 106,
    114, 121, 129, 137, 145, 153, 162, 171, 180, 189, 199, 209, 220, 230, 242, 256
};

static int16_t tan256(int a)
{
    int neg = a < 0, i, f;
    int16_t t;
    if (neg) a = -a;
    if (a > 8191) a = 8191;
    i = a >> 8;
    f = a & 255;
    t = tan_tab[i] + (int16_t)(((tan_tab[i + 1] - tan_tab[i]) * f) >> 8);
    return neg ? -t : t;
}

/* ---------- camera (the original's camera_follow) ---------- */
static int32_t cam_d;                   /* the chase camera's offset, lagging the car */
static int cam_yaw, chase_view = -1;

static int car_yaw_off(const Car *c)
{
    int32_t v = c->v > 8 ? c->v : 8;
    return sc_atan_ratio(c->lat, v);
}

int16_t render_horizon(int view)
{
    /* VIEW_CY - FOV * tan(pitch): pitch 1150 (chase), 260 (cockpit); in Layer 2 rows */
    return view == CAM_COCKPIT ? (VIEW_CY - 4) / 2 : (VIEW_CY - 15) / 2;
}

void camera_follow(const World *w)
{
    const Car *car = &w->car;
    int target;
    cam.car_dir = road_dir(w, car->z);
    if (w->view == CAM_COCKPIT) {
        int32_t z = car->z + (4 * (1 << FBITS));
        cam.x = road_cx(w, z) + (car->d >> FBITS);
        cam.z = z >> FBITS;
        cam.y = road_y(w, z) + 17;
        cam.yaw = cam.car_dir + car_yaw_off(car);
        chase_view = CAM_COCKPIT;
    } else {
        int32_t z = car->z - ((int32_t)CAM_BACK * (1 << FBITS));
        int16_t y0, y1;
        if (z < 0) z = 0;
        if (chase_view != CAM_CHASE) { cam_d = car->d; cam_yaw = cam.car_dir; }
        cam_d += (car->d - cam_d) / 4;
        target = cam.car_dir + car_yaw_off(car) / 3;
        cam_yaw += (int16_t)(target - cam_yaw) / 3;
        cam.x = road_cx(w, z) + (cam_d >> FBITS);
        cam.z = z >> FBITS;
        y0 = road_y(w, z);
        y1 = road_y(w, car->z);
        cam.y = (y0 > y1 ? y0 : y1) + CAM_UP;
        cam.yaw = cam_yaw;
        chase_view = CAM_CHASE;
    }
    cam.tan = tan256(cam.yaw);
    cam.horizon = render_horizon(w->view);
}

/* ---------- projection ---------- */
#define MAXR 24                         /* rails of a segment's flat band */

#define MAXD 22                         /* rails added for a segment (lines, the path, checks) */

typedef struct {
    int16_t bx, by;                     /* camera-relative x (sheared) and y of the centre line */
    uint16_t s;                         /* persp_scale(depth) */
    int16_t y0;                         /* screen row of the road surface */
    int16_t x[MAXR + MAXD];             /* the rails on screen: static ones, then the segment's */
    int16_t py[8];                      /* the rows of the cross-section's points (rail k's x) */
} Edge;

static int16_t prof[16];                /* the cross-section: (offset, height) pairs */
static uint8_t prof_id;
static int16_t roff[MAXR];              /* the static rails: offsets from the centre line */
static uint8_t nroff;                   /* how many */
static int16_t profh[8];                /* the points' heights (port: projected per edge in one go) */
static uint8_t rline;                   /* the first line rail: line n is rails rline + 2n, +1 */
static Edge edges[2];
static Edge *pa = &edges[0], *pb = &edges[1];   /* near and far edge of the segment drawn */
#define ea (*pa)
#define eb (*pb)
static int16_t q[8];                    /* a quad */
static uint8_t nrails;
static uint8_t spans[3 * 24];
static uint8_t nspans;

#define ex(e, x)    persp_x1((x), (e)->bx, (e)->s, VIEW_CX)
#define ey(e, lift) persp_y1((lift), (e)->by, (e)->s >> 1, cam.horizon)    /* (half height) */

/* the edge of a segment at world (cx, y) and depth zr (>= NEAR) */
static void edge_at(Edge *e, int32_t cx, int16_t y, int16_t zr)
{
    int32_t bx = cx - cam.x - (((int32_t)zr * cam.tan) >> 8);
    if (bx > 30000) bx = 30000;
    if (bx < -30000) bx = -30000;
    e->bx = (int16_t)bx;
    e->by = y - cam.y;
    e->s = persp_scale(zr);
    persp_xs(roff, nroff, e->bx, e->s, VIEW_CX, e->x);
    persp_ys(profh, 8, e->by, e->s >> 1, cam.horizon, e->py);       /* (half height) */
    e->y0 = e->py[3];                   /* (the road's edge: height 0) */
}

static void poly4(uint8_t col)
{
    l2p_poly(q, 4, col);
    render_polys++;
}

/* a quad between the edges: cross-section points k0, k1 (projected with the edges) */

static void quad(uint8_t k0, uint8_t k1, uint8_t col)
{
    q[0] = ea.x[k0]; q[1] = ea.py[k0];
    q[2] = ea.x[k1]; q[3] = ea.py[k1];
    q[4] = eb.x[k1]; q[5] = eb.py[k1];
    q[6] = eb.x[k0]; q[7] = eb.py[k0];
    /* off to one side entirely: nothing to fill */
    if ((q[0] < 0 && q[2] < 0 && q[4] < 0 && q[6] < 0) ||
        (q[0] > 255 && q[2] > 255 && q[4] > 255 && q[6] > 255)) return;
    poly4(col);
}

/* a vertical band along the segment (guardrail, barrier) at screen x xa (near), xb (far) */
static void wall(int16_t xa, int16_t xb, int16_t y0, int16_t y1, uint8_t col)
{
    q[0] = xa; q[1] = ey(&ea, y0);
    q[2] = xa; q[3] = ey(&ea, y1);
    q[4] = xb; q[5] = ey(&eb, y1);
    q[6] = xb; q[7] = ey(&eb, y0);
    poly4(col);
}

/* the band: static rails come from the edges; more can be added per segment */
static void band_begin(void)
{
    nrails = nroff;
    nspans = 0;
}

static uint8_t band_rail(int16_t far_off, int16_t near_off)    /* a rail of this segment only */
{
    eb.x[nrails] = ex(&eb, far_off);
    ea.x[nrails] = ex(&ea, near_off);
    return nrails++;
}

static void band_span(uint8_t l, uint8_t r, uint8_t col)
{
    uint8_t *p = spans + nspans * 3;
    p[0] = l; p[1] = r; p[2] = col;
    nspans++;
}

static void band_end(void)
{
    if (nspans && !(render_skip & 4)) {
        l2p_band(eb.y0, ea.y0, eb.x, ea.x, nrails, spans, nspans);
        render_polys++;
    }
}

/* ---------- the road ---------- */
#define FLAT_SIDE(t) ((t) == SIDE_GRASS || (t) == SIDE_MEDIAN || (t) == SIDE_TOWN)
static uint8_t no_lines;                /* far: no lane lines (LINE_SEGS) */
#define SIDE_IN_BAND(t) FLAT_SIDE(t)
#define FAR_OUT 20000                   /* port: the outermost points, off screen at any depth */

/* the cross-section, and the rails of the flat band (rebuilt when the sides change) */
static void profile(const World *w, const Seg *g)
{
    const int16_t *L = side_prof[g->side[0]], *R = side_prof[g->side[1]];
    const RouteInfo *r = &route_info[w->route];
    int16_t rl = road_left(w), rr = road_right(w);
    uint8_t id = (g->side[0] * SIDE_COUNT + g->side[1]) | no_lines << 7, k, n;
    if (id == prof_id) return;
    prof_id = id;
    prof[6] = rl;                prof[7] = 0;             /* 3: road left edge */
    prof[8] = rr;                prof[9] = 0;             /* 4: road right edge */
    prof[4] = rl - SHOULDER;     prof[5] = 0;             /* 2 */
    prof[10] = rr + SHOULDER;    prof[11] = 0;            /* 5 */
    prof[2] = prof[4] - L[0];    prof[3] = L[1];          /* 1 */
    prof[0] = -FAR_OUT;          prof[1] = L[3];          /* 0 (port: was offset 2 - L[2]) */
    prof[12] = prof[10] + R[0];  prof[13] = R[1];         /* 6 */
    prof[14] = FAR_OUT;          prof[15] = R[3];         /* 7 */
    /* rails: the eight points (rail k is point k: port, all of them, so quads
       and strips take their x from the edges), then the lines' edges */
    for (k = 0; k < 8; k++) {
        roff[k] = prof[k * 2];
        profh[k] = prof[k * 2 + 1];
    }
    nroff = 8;
    rline = nroff;
    if (no_lines) return;
#define LINE_RAILS(x) (roff[nroff++] = (x) - 2, roff[nroff++] = (x) + 2)
    LINE_RAILS(r->no ? 0 : rl + 4);                       /* line 0: centre / left yellow */
    LINE_RAILS(rr - 3);                                   /* line 1: right edge */
    LINE_RAILS(rl + 3);                                   /* line 2: left edge (two-way) */
    for (n = 1; n < r->nl; n++) LINE_RAILS(n * LANE_W);   /* lines 3..: lanes */
    for (n = 1; n < r->no; n++) LINE_RAILS(-n * LANE_W);
#undef LINE_RAILS
}

static uint8_t fog;                     /* added to the pens of far segments */

static void side_strips(const World *w, int i, int side)
{
    int t = w->seg[i].side[side], alt = i & 1, alt2 = ((i >> 1) + (w->frame >> 4)) & 1;
    uint8_t inner, outer, ko = side ? 6 : 0, ki = side ? 5 : 1;
    switch (t) {
    case SIDE_LEVEE: inner = PEN_GRASS + alt; outer = PEN_WATER + alt2; break;
    case SIDE_DROP:  inner = PEN_DIRT + alt;  outer = PEN_GRASS + 1; break;
    case SIDE_WALL:  inner = alt ? PEN_ROCK : PEN_DIRT; outer = PEN_GRASS; break;
    case SIDE_CLIFF: inner = alt ? PEN_ROCK : PEN_DIRT; outer = PEN_HAZE; break;
    case SIDE_TOWN:  inner = PEN_PAVE; outer = PEN_PAVE; break;
    case SIDE_CANAL: inner = PEN_ROCK; outer = PEN_WATER + alt2; break;
    default:         inner = PEN_GRASS + alt; outer = PEN_GRASS; break;
    }
    if (SIDE_IN_BAND(t)) {              /* port: in the flat band (heights of 8 units or less) */
        band_span(ko, ko + 1, outer + fog);
        band_span(ki, ki + 1, inner + fog);
        return;
    }
    /* sloping: the outer strip, drawn before the band, as a band of its own at
       its height (port: the original's quad; its two points' heights differ
       by 20 units at most: the mean), and the slope as a quad, near only
       (port: further out the ground fill shows between, as in the original
       where nothing covers it) */
    {
        static uint8_t sp1[3] = { 0, 1, 0 };
        sp1[2] = outer + fog;
        if (!(render_skip & 32))
            l2p_band((eb.py[ko] + eb.py[ko + 1]) >> 1, (ea.py[ko] + ea.py[ko + 1]) >> 1,
                     &eb.x[ko], &ea.x[ko], 2, sp1, 1);
        render_polys++;
    }
    if (!(render_skip & 64)) quad(ki, ki + 1, inner + fog);
}

static int far_pen(int side)
{
    switch (side) {
    case SIDE_LEVEE: case SIDE_CANAL: return PEN_WATER;
    case SIDE_CLIFF: return PEN_HAZE;
    case SIDE_TOWN: return PEN_PAVE;
    case SIDE_DROP: return PEN_GRASS + 1;
    default: return PEN_GRASS;
    }
}

/* ---------- scenery: the original's boxes and pyramids, flat on ---------- */
/* port: seen from the road, flat on: a box is its front face (with its side
   for the big ones), a pyramid a triangle in its lit colour (the original's
   are one colour) */
static Edge sf;                          /* the depth scenery is being drawn at */

static uint8_t scene_at(int32_t wx, int16_t wy, int16_t zr)
{
    int32_t bx;
    if (zr < NEAR) return 0;
    bx = wx - cam.x - (((int32_t)zr * cam.tan) >> 8);
    if (bx > 30000 || bx < -30000) return 0;
    sf.bx = (int16_t)bx;
    sf.by = wy - cam.y;
    sf.s = persp_scale(zr);
    return 1;
}

static void front(int16_t x0, int16_t x1, int16_t y0, int16_t y1, uint8_t col)
{
    l2p_rect(ey(&sf, y1), ey(&sf, y0), ex(&sf, x0), ex(&sf, x1), col);
    render_polys++;
}

static void box(int32_t wx, int16_t wy, int16_t zr, int16_t x0, int16_t x1, int16_t y0, int16_t y1,
                int16_t d, uint8_t col, uint8_t side)
{
    if (!scene_at(wx, wy, zr)) return;
    if (side) {
        Edge b = sf;
        int16_t x = sf.bx + x1 < 0 ? x1 : sf.bx + x0 > 0 ? x0 : 0x7FFF;
        if (x != 0x7FFF) {
            b.s = persp_scale(zr + d);
            b.bx = (int16_t)(wx - cam.x - (((int32_t)(zr + d) * cam.tan) >> 8));
            q[0] = ex(&sf, x); q[1] = ey(&sf, y0); q[2] = ex(&b, x); q[3] = ey(&b, y0);
            q[4] = q[2]; q[5] = ey(&b, y1); q[6] = q[0]; q[7] = ey(&sf, y1);
            poly4(side);
        }
    }
    front(x0, x1, y0, y1, col);
}

static void tri(int16_t x0, int16_t x1, int16_t y0, int16_t ya, uint8_t col)   /* at sf's depth */
{
    q[0] = ex(&sf, x0); q[1] = ey(&sf, y0);
    q[2] = ex(&sf, x1); q[3] = q[1];
    q[4] = ex(&sf, (x0 + x1) / 2); q[5] = ey(&sf, ya);
    l2p_poly(q, 3, col);
    render_polys++;
}

static void pyramid(int32_t wx, int16_t wy, int16_t zr, int16_t x0, int16_t x1, int16_t y0, int16_t ya,
                    uint8_t col)
{
    if (scene_at(wx, wy, zr)) tri(x0, x1, y0, ya, col);
}

enum { SC_NONE, SC_TREE, SC_PINE, SC_POLE, SC_BOULDER, SC_LAMP, SC_HOUSE, SC_BLOCK };

static void scenery_model(int t, int32_t x, int16_t y, int16_t zr, int side)
{
    switch (t) {
    case SC_TREE:                       /* (port: the canopy at the trunk's depth: one scale) */
        if (!scene_at(x, y, zr)) break;
        front(-3, 3, 0, 30, PEN_BROWN + fog);
        tri(-28, 28, 28, 110, PEN_TREE + fog);
        break;
    case SC_PINE:
        if (!scene_at(x, y, zr)) break;
        front(-3, 3, 0, 16, PEN_BROWN + fog);
        tri(-20, 20, 14, 140, PEN_TREE + fog);
        break;
    case SC_POLE:
        if (!scene_at(x, y, zr)) break;
        front(-2, 2, 0, 150, PEN_BROWN + fog);
        front(-22, 22, 136, 140, PEN_BROWN + fog);
        break;
    case SC_LAMP:
        if (!scene_at(x, y, zr)) break;
        front(-2, 2, 0, 150, PEN_GREY + fog);
        if (side) front(-34, 2, 146, 150, PEN_GREY + fog);
        else front(-2, 34, 146, 150, PEN_GREY + fog);
        break;
    case SC_BOULDER:
        pyramid(x, y, zr, -44, 44, 0, 66, PEN_ROCK + fog);
        break;
    case SC_HOUSE:
        box(x, y, zr - 50, -50, 50, 0, 80, 100, PEN_CARW + fog, PEN_CARW + 1 + fog);
        pyramid(x, y, zr - 56, -56, 56, 80, 136, PEN_RED + fog);
        break;
    case SC_BLOCK:
        box(x, y, zr - 56, -56, 56, 0, 180, 112, PEN_GREY + fog, PEN_PANEL + fog);
        box(x, y, zr - 50, -50, 50, 180, 230, 100, PEN_GLASS + fog, 0);
        break;
    }
}

/* scenery beside segment i, the same every time (hashed from i);
   port: what is there is worked out once and kept (the hash's 32-bit
   arithmetic is slow on the Z80) */
typedef struct { int16_t seg; uint8_t route, mdl[2], dz[2]; int16_t out[2], y[2]; } Scene;
static Scene scache[32];

static const Scene *scene_of(const World *w, int i)
{
    Scene *c = &scache[i & 31];
    const Seg *s = &w->seg[i];
    int side;
    if (c->seg == i && c->route == w->route) return c;
    c->seg = i;
    c->route = w->route;
    for (side = 0; side < 2; side++) {
        uint32_t h = sc_hash((uint32_t)i * 2 + side + w->route * 977);
        int t = s->side[side], mdl = SC_NONE;
        int16_t out = 0, y = 0, dz = (int16_t)(h >> 8 & 63);
        switch (t) {
        case SIDE_GRASS:
            if (h % 3 == 0) { mdl = SC_TREE; out = 90 + (int16_t)(h >> 12 & 511); y = -4; }
            break;
        case SIDE_LEVEE:
            if (side == 1 && i % 6 == 0) { mdl = SC_POLE; out = 10; }
            break;
        case SIDE_DROP:
            if (h % 3 == 0) { mdl = SC_TREE; out = 240 + (int16_t)(h >> 12 & 511); y = -206; }
            else if (i % 6 == 3) { mdl = SC_POLE; out = 10; }
            break;
        case SIDE_WALL:
            if (h % 3 == 0) { mdl = SC_PINE; out = 60 + (int16_t)(h >> 12 & 255); y = 200; }
            else if (h % 7 == 1) { mdl = SC_BOULDER; out = 6; }
            break;
        case SIDE_CLIFF:
            if (h % 2 == 0) { mdl = SC_PINE; out = 200 + (int16_t)(h >> 12 & 1023); y = -515; }
            break;
        case SIDE_MEDIAN:
            if (i % 4 == 0) { mdl = SC_LAMP; out = 14; }
            break;
        case SIDE_TOWN:
            if (i % 2 == 0) { mdl = (h & 4) ? SC_BLOCK : SC_HOUSE; out = 120; y = 3; dz = 0; }
            break;
        }
        c->mdl[side] = (uint8_t)mdl;
        c->out[side] = out;
        c->y[side] = y;
        c->dz[side] = (uint8_t)dz;
    }
    return c;
}

static void scenery_side(const World *w, int i, uint8_t mdl, int16_t edge_x, int16_t y, uint8_t dz, int side)
{
    int32_t z = (int32_t)i * SEG_LEN + dz, zf = z * (1 << FBITS);
    scenery_model(mdl, road_cx(w, zf) + edge_x, road_y(w, zf) + y, (int16_t)(z - cam.z), side);
}

static void scenery(const World *w, int i)
{
    const Scene *c = scene_of(w, i);
    if (c->mdl[0] != SC_NONE)
        scenery_side(w, i, c->mdl[0], road_left(w) - SHOULDER - c->out[0], c->y[0], c->dz[0], 0);
    if (c->mdl[1] != SC_NONE)
        scenery_side(w, i, c->mdl[1], road_right(w) + SHOULDER + c->out[1], c->y[1], c->dz[1], 1);
}

/* segment i, or i and i + 1 as one (n = 2: far away, nothing differs on them) */
static void draw_segment(const World *w, int i, int k, int16_t zra, int16_t zrb, uint8_t share, uint8_t n2)
{
    const Seg *s = &w->seg[i], *t = &w->seg[i + n2];
    const RouteInfo *r = &route_info[w->route];
    int16_t rl = road_left(w), rr = road_right(w);
    int flood = s->flags & SF_FLOOD, n, lane;
    uint8_t road_pen, sh_pen, old_id = prof_id;
    int16_t cd;

    no_lines = k >= LINE_SEGS;
    profile(w, s);
    /* the far edge is the last segment's near one if nothing differs (port) */
    {
        Edge *e = pa;
        pa = pb;
        pb = e;
    }
    if (!(share && prof_id == old_id)) edge_at(&eb, t->cx, t->y, zrb);
    if (zra >= NEAR) edge_at(&ea, s->cx, s->y, zra);
    else {
        /* the near edge behind the near plane: cut it at NEAR */
        int16_t f = NEAR - zra;          /* of SEG_LEN */
        edge_at(&ea, s->cx + (((t->cx - s->cx) * f) >> SEG_BITS),
                s->y + (int16_t)(((int32_t)(t->y - s->y) * f) >> SEG_BITS), NEAR);
    }

    band_begin();
    if (!(render_skip & 2)) {
        side_strips(w, i, 0);
        side_strips(w, i, 1);
    }

    if (flood) {
        road_pen = PEN_WATER + (((i >> 1) + (w->frame >> 3)) & 1);
        sh_pen = road_pen;
    } else if (s->flags & SF_OVERPASS) {
        road_pen = sh_pen = PEN_SHADOW;           /* the shadow that fools it */
    } else {
        road_pen = PEN_ROAD + ((i >> 1) & 1);
        sh_pen = PEN_ROAD + 1;
    }
    band_span(2, 3, sh_pen + fog);
    band_span(4, 5, sh_pen + fog);
    band_span(3, 4, road_pen + fog);

    /* the finish line: a chequered band */
    if (s->flags & SF_FINISH) {
        int16_t x;
        for (x = rl; x < rr; x += 16) {
            uint8_t l = band_rail(x, x);
            band_span(l, band_rail(x + 8, x + 8), ((x >> 4) + i) & 1 ? PEN_WHITE : PEN_BLACK);
        }
    }

    if (!no_lines && !flood) {
        int ghost = s->flags & SF_GHOST;
        uint8_t real = ghost ? PEN_ROAD + 1 : PEN_WHITE;    /* worn away where the old ones are */
#define LINE(n, col) band_span(rline + 2 * (n), rline + 2 * (n) + 1, col)
        LINE(0, r->no && ghost ? PEN_ROAD + 1 : PEN_YELLOW);
        for (n = 1; n < r->nl; n++)
            if (!(i & 1) && n2 == 1) LINE(2 + n, PEN_WHITE);
        for (n = 1; n < r->no; n++)
            if (!(i & 1) && n2 == 1) LINE(1 + r->nl + n, PEN_WHITE);
        LINE(1, real);
        if (r->no) LINE(2, PEN_WHITE);
#undef LINE
        if (ghost) {
            /* the old lines: where the road used to go */
            int16_t g0 = s->ghost, g1 = (t->flags & SF_GHOST) ? t->ghost : g0;
            if (!(i & 1)) {
                uint8_t l = band_rail(g1 - 2, g0 - 2);
                band_span(l, band_rail(g1 + 2, g0 + 2), PEN_YELLOW);
                l = band_rail(rr - 5 + g1, rr - 5 + g0);
                band_span(l, band_rail(rr - 1 + g1, rr - 1 + g0), PEN_WHITE);
            }
        }
    }

    /* the autopilot's planned path (port: in the band, lift 1 dropped) */
    if (w->ai.on && w->state != GS_GAMEOVER) {
        int p = i - w->ai.plan_from;
        if (p >= 0 && p < 12 && (!w->ai.blinded || (w->frame & 2))) {
            int16_t a = w->ai.plan[p] >> FBITS, b = w->ai.plan[p + 1] >> FBITS;
            uint8_t l = band_rail(b - 3, a - 3);
            band_span(l, band_rail(b + 3, a + 3), w->ai.blinded ? PEN_WHITE : PEN_AI);
        }
    }
    band_end();

    if (render_skip & 16) goto done;
    if (s->flags & SF_RAIL_L) {
        wall(ea.x[2], eb.x[2], 6, 12, PEN_GREY + fog);
        wall(ea.x[2], eb.x[2], 0, 6, PEN_PANEL + fog);
    }
    if (s->flags & SF_RAIL_R) {
        wall(ea.x[5], eb.x[5], 6, 12, PEN_GREY + fog);
        wall(ea.x[5], eb.x[5], 0, 6, PEN_PANEL + fog);
    }
    if (s->side[0] == SIDE_MEDIAN) wall(ex(&ea, prof[4] - 8), ex(&eb, prof[4] - 8), 0, 14, PEN_GREY + fog);

    /* cones: in the closed lane, and along its line (sprites) */
    if (s->flags & SF_CONES) {
        int32_t z = ((int32_t)i << SEG_BITS);
        for (lane = 0; lane < 8 && !(s->closed & (1 << lane)); lane++)
            ;
        spr_add_world(w, SP_CONE, (z + 64) * (1 << FBITS), (lane * LANE_W + 4) * (1 << FBITS), 0, 0);
        if (cone_at(w, i, &cd)) {
            int hit = w->knocked[i >> 3] & (1 << (i & 7));
            spr_add_world(w, SP_CONE, (z + (hit ? 30 : 0)) * (1 << FBITS), (cd + (hit ? 26 : 0)) * (1 << FBITS), 0, 0);
        }
    }

    if (k < SCENE_SEGS && !(render_skip & 1)) {
        uint8_t j;
        for (j = n2; j-- > 0; ) scenery(w, i + j);
    }

    if ((s->flags & SF_OVERPASS) && !(w->seg[i - 1 >= 0 ? i - 1 : 0].flags & SF_OVERPASS)) {
        int32_t zf = ((int32_t)(i + 1) << SEG_BITS) * (1 << FBITS);
        int32_t x = road_cx(w, zf) + (rl + rr) / 2;
        int16_t y = road_y(w, zf), zr = zrb;
        box(x, y, zr - 10, -150, -128, 0, 104, 20, PEN_GREY + fog, PEN_PANEL + fog);
        box(x, y, zr - 10, 128, 150, 0, 104, 20, PEN_GREY + fog, PEN_PANEL + fog);
        box(x, y, zr - 26, -170, 170, 104, 128, 52, PEN_GREY + fog, 0);
    }
    if (i == w->finish) {
        int32_t x = s->cx + (rl + rr) / 2;
        box(x, s->y, zra - 4, -150, -140, 0, 130, 8, PEN_YELLOW, 0);
        box(x, s->y, zra - 4, 140, 150, 0, 130, 8, PEN_YELLOW, 0);
        box(x, s->y, zra - 3, -150, 150, 104, 134, 6, PEN_RED, 0);
    }
done:
    spr_segment_done(i, eb.y0);
}

/* ---------- the sky ---------- */
static int sun_yaw, sun_route = -1, sky_route = -1;

/* the original's skyline: column j's height (port: in rows of the 80) */
static uint8_t skyline_h(int r, int j)
{
    uint32_t h = sc_hash((uint32_t)(r * 64 + j));
    int v;
    switch (r) {
    case 0: v = 4 + (int)(h % 5); break;                        /* trees across the river */
    case 1: v = (j & 3) ? 3 + (int)(h % 4) : 8 + (int)(h % 14); break;  /* distant towers */
    case 2: v = 10 + (int)(h % 30); break;                      /* mountains */
    default: v = 6 + (int)(h % 12); break;                      /* rooftops */
    }
    return (uint8_t)((v * 3) / 8);
}

/* screen x of a yaw (16-bit) relative to the camera: FOV * angle (radians) */
static int16_t yaw_x(int16_t d)
{
    return VIEW_CX + (int16_t)(((int32_t)d * FOV) / 10430);
}

/* the panorama: SKY_W x SKY_PH bytes in pages SKY_PAGE.. (mapped at 0x6000 a page at a time) */
static void sky_put(uint16_t o, uint16_t n, uint8_t v)
{
    while (n) {
        uint16_t off = o & 0x1FFF, k = 0x2000 - off;
        if (k > n) k = n;
        ZXN_WRITE_REG(0x53, SKY_PAGE + (o >> 13));
        memset((uint8_t *)0x6000 + off, v, k);
        o += k;
        n -= k;
    }
}

/* the original's skyline columns (1024 of yaw each, sloping tops, or flat
   for the towers and roofs), drawn into the panorama: row rr is SKY_PH - rr
   rows above the horizon */
static void sky_build(int r)
{
    uint8_t rr, j, blocky = r == 1 || r == 3, sh[64];      /* (on the stack: page 38 is full) */
    for (j = 0; j < 64; j++) sh[j] = skyline_h(r, j);
    for (rr = 0; rr < SKY_PH; rr++) {
        uint16_t base = rr * SKY_W;
        int16_t lv = SKY_PH - rr;
        sky_put(base, SKY_W, PEN_SKY);
        for (j = 0; j < 64; j++) {
            int16_t x0 = (int16_t)(((uint32_t)j * 1024 * FOV) / 10430);
            int16_t x1 = j == 63 ? SKY_W : (int16_t)(((uint32_t)(j + 1) * 1024 * FOV) / 10430);
            int16_t h0 = sh[j], h1 = blocky ? h0 : sh[(j + 1) & 63], a = x0, b = x1;
            if (h0 < lv && h1 < lv) continue;
            if (h0 >= lv && h1 < lv) b = x0 + (x1 - x0) * (h0 - lv + 1) / (h0 - h1);
            else if (h0 < lv) a = x1 - (x1 - x0) * (h1 - lv + 1) / (h1 - h0);
            if (b > a) sky_put(base + a, b - a, PEN_HAZE);
        }
    }
    ZXN_WRITE_MMU3(11);
}

static int16_t oct[16];

static void octagon(int16_t cx, int16_t cy, int16_t r, uint8_t col)    /* (half height) */
{
    int16_t h = (r * 5) / 12, ry = r / 2, hy = h / 2;
    oct[0] = cx - h; oct[1] = cy - ry;  oct[2] = cx + h;  oct[3] = cy - ry;
    oct[4] = cx + r; oct[5] = cy - hy;  oct[6] = cx + r;  oct[7] = cy + hy;
    oct[8] = cx + h; oct[9] = cy + ry;  oct[10] = cx - h; oct[11] = cy + ry;
    oct[12] = cx - r; oct[13] = cy + hy; oct[14] = cx - r; oct[15] = cy - hy;
    l2p_poly(oct, 8, col);
}

static void draw_sun(const World *w, int16_t hz)
{
    int16_t d, x, r;
    if (sun_yaw < 0) return;
    d = (int16_t)(sun_yaw - cam.yaw);
    if (d > 9000 || d < -9000) return;
    x = yaw_x(d);
    r = 7 + w->glare / 2;                        /* port: 9 + glare / 2 at 212 rows */
    octagon(x, hz - 5, r + 3, PEN_ORANGE);
    octagon(x, hz - 5, r, PEN_YELLOW);
    octagon(x, hz - 5, r / 2, PEN_WHITE);
}

void render_init(void)
{
    uint8_t k;
    for (k = 0; k < 32; k++) scache[k].seg = -1;
    persp_fov(FOV);
}

#define LOD_SEGS 6                      /* port: segments in pairs from here */
#define LOD4_SEGS 14                    /* and in fours */
#define LOD_FLAGS (SF_FINISH | SF_OVERPASS | SF_GHOST | SF_CONES)

static uint8_t mergeable(const Seg *a, const Seg *b)
{
    return a->side[0] == b->side[0] && a->side[1] == b->side[1] && a->flags == b->flags &&
           !(a->flags & LOD_FLAGS);
}

void render_world(const World *w)
{
    int s0, s1, i;
    int16_t hz = cam.horizon, vanish_x, yfar = hz;
    int32_t zr;

    render_polys = 0;
    if (w->route != sun_route) {
        /* the sun sits where the road heads into it */
        sun_route = w->route;
        sun_yaw = -1;
        for (i = 0; i < w->nhaz; i++)
            if (w->haz[i].type == HZ_GLARE) {
                sun_yaw = road_dir(w, ((int32_t)w->haz[i].seg + w->haz[i].len / 2) * SEGF);
                break;
            }
    }

    s0 = seg_of(cam.z * (1 << FBITS));
    if (s0 < 0) s0 = 0;
    s1 = s0 + DRAW_SEGS;
    if (s1 > w->nseg - 2) s1 = w->nseg - 2;

    /* where the sky meets the ground: the far end's centre at the eye's height */
    zr = ((int32_t)s1 << SEG_BITS) - cam.z;
    {
        int32_t bx = w->seg[s1].cx + (road_left(w) + road_right(w)) / 2 - cam.x - ((zr * cam.tan) >> 8);
        uint16_t sc = persp_scale((uint16_t)zr);
        vanish_x = zr > NEAR && bx > -30000 && bx < 30000 ? VIEW_CX + persp_mul((int16_t)bx, sc) : VIEW_CX;
        if (zr > NEAR) yfar = hz - persp_mul(w->seg[s1].y - cam.y, sc >> 1) + 1;
    }
    if (vanish_x < 0) vanish_x = 0;
    if (vanish_x > VIEW_W - 1) vanish_x = VIEW_W - 1;

    if (w->route != sky_route) {
        sky_route = w->route;
        sky_build(w->route);
    }
    {
        /* the panorama's column at the view's left edge (port: see sky_build) */
        int16_t p0 = (int16_t)(((uint32_t)(uint16_t)cam.yaw * FOV) / 10430) - VIEW_CX;
        if (p0 < 0) p0 += SKY_W;
        if (!(render_skip & 16)) sky_copy((uint16_t)p0, hz - SKY_PH);    /* (before l2poly sets up the DMA) */
    }
    l2p_begin();
    l2p_top = 0;
    l2p_bot = VIEW_ROWS;
    if (!(render_skip & 8)) l2p_rect(0, hz - SKY_PH, 0, VIEW_W - 1, PEN_SKY);
    /* the ground: the right side's colour across (one DMA a page), then the left */
    if (!(render_skip & 8)) {
        uint8_t gl = far_pen(w->seg[s1].side[0]), gr = far_pen(w->seg[s1].side[1]);
        l2p_rect(hz, VIEW_ROWS, 0, VIEW_W - 1, gr);
        if (gl != gr) l2p_rect(hz, VIEW_ROWS, 0, vanish_x, gl);
    }
    if (!(render_skip & 16)) draw_sun(w, hz);

    spr_frame_begin(w, s0, s1);
    prof_id = 0xFF;
    for (i = s1; i >= s0; i--) {
        int16_t zra = (int16_t)(((int32_t)i << SEG_BITS) - cam.z);
        int16_t zrb = zra + SEG_LEN;
        int k = i - s0;
        uint8_t n2 = 1, most = k >= LOD4_SEGS ? 4 : k >= LOD_SEGS ? 2 : 1;
        if (zrb < NEAR) continue;
        fog = k >= 26 ? 3 * 32 : k >= 20 ? 2 * 32 : k >= 13 ? 32 : 0;
        while (n2 < most && i - 1 > s0 && mergeable(&w->seg[i - 1], &w->seg[i])) {
            /* port: far away, two (further, four) segments drawn as one */
            i--; k--;
            zra -= SEG_LEN;
            n2++;
        }
        draw_segment(w, i, k, zra, zrb, i + n2 <= s1, n2);
    }
    l2p_end();
    spr_frame_end(w);
}
