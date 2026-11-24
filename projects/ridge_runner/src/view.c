/*
 * view.c - Ridge Runner's scene: the original's render.c (camera, ground,
 * track, gates, shadows, far ridges, painter's order) as polygons for
 * the Next's Layer 2, and the bikes as sprites. See view.h.
 *
 * Integer arithmetic only, the same on the host (tools/view_host.c draws
 * it into a picture the test compares with the Next's) and on the Z80.
 */
#include <string.h>
#include "view.h"

#define CELL     80
#define SEG      80                     /* the track ribbon's pieces along the course */
#define WIN      4                      /* ground cells each side (the original's 68k value) */
#define NV       (2 * WIN + 2)          /* grid points a side */
#define U        16                     /* camera space is in sixteenths of a unit */
#define NEAR     (12 * U)
#define FOV      210
/* port: the original's 320 x 256 projection, scaled to 256 x 192 */
#define FOVX     168                    /* 210 * 4 / 5 */
#define FOVY     158                    /* 210 * 3 / 4 */
#define CXS      128                    /* 160 * 4 / 5 */
#define CYS      78                     /* 104 * 3 / 4 (full height) */
#define PITCH    1500

/* ---- the palettes: the original's three themes (12-bit RGB) ---- */
const uint16_t view_themes[3][33] = {
    { 0x000, 0xFFF, 0x123, 0x99A, 0x69D, 0x7C5, 0x6B4, 0x5A3, 0x492, 0x381, 0x260,
      0x998, 0x887, 0x776, 0xE33, 0x131, 0x3CF, 0x18A, 0xF62, 0xA31, 0xFD3, 0xA81, 0xC5E, 0x737,
      0xF80, 0xFFE, 0xF33, 0xFE4, 0x6F6, 0x5EF, 0x58A, 0xFC4, 0xA50 },
    { 0x000, 0xFFF, 0x211, 0xA99, 0xE96, 0xDA6, 0xC95, 0xB84, 0xA73, 0x862, 0x641,
      0x887, 0x776, 0x665, 0xE33, 0x420, 0x3CF, 0x18A, 0x6E4, 0x382, 0xFD3, 0xA81, 0xC5E, 0x737,
      0xF80, 0xFFE, 0xF33, 0xFE4, 0x6F6, 0x5EF, 0xB75, 0xFC4, 0xA50 },
    { 0x000, 0xFFF, 0x123, 0x99A, 0x8AC, 0xEEF, 0xDDE, 0xBCD, 0x9AC, 0x89A, 0x678,
      0x667, 0x556, 0x445, 0xE33, 0x567, 0xF62, 0xA31, 0x3CF, 0x18A, 0xFD3, 0xA81, 0xC5E, 0x737,
      0xF80, 0xFFE, 0xF33, 0xFE4, 0x6F6, 0x5EF, 0x9AB, 0xFC4, 0xA50 },
};
static const uint8_t track_theme[RR_TRACKS] = { 0, 1, 1, 2, 0, 2 };
const uint16_t view_sky_top[3] = { 0x36B, 0xC74, 0x68A }, view_sky_bottom[3] = { 0xBDE, 0xFDA, 0xDEF };

uint8_t view_theme(int32_t track) { return track_theme[(track - 1) % RR_TRACKS]; }

/* ---- the camera (the original's Cam with cam_place: sixteenths) ---- */
typedef struct { int16_t x, y, z; } V3;

static int32_t camx, camy, camz;        /* whole units */
static int16_t qx, qy, qz;              /* sixteenths past them */
static int16_t CY, SY, CP, SP;          /* cos / sin >> 4 (4096 = 1) */
static int16_t horizon;                 /* full-height row */

static void xform(int32_t wx, int32_t wy, int32_t wz, V3 *o)
{
    int16_t dx = (int16_t)((wx - camx) * U - qx), dy = (int16_t)((wy - camy) * U - qy),
            dz = (int16_t)((wz - camz) * U - qz);
    int16_t rx = (int16_t)(((int32_t)dx * CY - (int32_t)dz * SY) >> 12);
    int16_t rz = (int16_t)(((int32_t)dx * SY + (int32_t)dz * CY) >> 12);
    o->x = rx;
    o->y = (int16_t)(((int32_t)dy * CP + (int32_t)rz * SP) >> 12);
    o->z = (int16_t)(((int32_t)rz * CP - (int32_t)dy * SP) >> 12);
}

/* camera space -> screen (x full width, y half height) */
static void project(const V3 *p, int16_t *sx, int16_t *sy)
{
    int32_t k = ((int32_t)1 << 20) / p->z, x, y;            /* p->z >= NEAR */
    x = CXS + (((int32_t)p->x * FOVX >> 8) * k >> 12);
    y = CYS - (((int32_t)p->y * FOVY >> 8) * k >> 12);
    if (x < -8000) x = -8000;
    if (x > 8000) x = 8000;
    if (y < -8000) y = -8000;
    if (y > 8000) y = 8000;
    *sx = (int16_t)x;
    *sy = (int16_t)(y >> 1);
}

/* ---- the polygons ---- */
#define MAXPOLY 170
#define MAXXY   (MAXPOLY * 7)
typedef struct { int16_t key; uint16_t at; uint8_t n, pen; } Poly;
static Poly poly[MAXPOLY];
static int16_t xy[MAXXY];
static uint16_t npoly, nxy;
static uint8_t order[MAXPOLY];

/* a polygon in camera space: clipped at the near plane, projected, kept */
static void add(const V3 *v, uint8_t n, int16_t key, uint8_t pen)
{
    V3 c[10];
    uint8_t m = 0, i;
    Poly *p;
    for (i = 0; i < n; i++) {
        const V3 *a = &v[i], *b = &v[i + 1 == n ? 0 : i + 1];
        uint8_t ain = a->z >= NEAR, bin = b->z >= NEAR;
        if (ain && m < 10) c[m++] = *a;
        if (ain != bin && m < 10) {
            /* from the inside end, so a shared edge clips to the same point */
            const V3 *in = ain ? a : b, *out = ain ? b : a;
            int32_t t = ((int32_t)(in->z - NEAR) << 12) / (in->z - out->z);
            c[m].x = in->x + (int16_t)(((int32_t)(out->x - in->x) * t) >> 12);
            c[m].y = in->y + (int16_t)(((int32_t)(out->y - in->y) * t) >> 12);
            c[m].z = NEAR;
            m++;
        }
    }
    if (m < 3 || npoly >= MAXPOLY || nxy + 2 * m > MAXXY) return;
    p = &poly[npoly++];
    p->key = key;
    p->at = nxy;
    p->n = m;
    p->pen = pen;
    for (i = 0; i < m; i++, nxy += 2) project(&c[i], &xy[nxy], &xy[nxy + 1]);
}

/* a polygon already on the screen (x full width, y half height) */
static void add_screen(const int16_t *s, uint8_t n, int16_t key, uint8_t pen)
{
    Poly *p;
    if (npoly >= MAXPOLY || nxy + 2 * n > MAXXY) return;
    p = &poly[npoly++];
    p->key = key;
    p->at = nxy;
    p->n = n;
    p->pen = pen;
    memcpy(&xy[nxy], s, 4 * n);
    nxy += 2 * n;
}

/* ---- the caches ---- */
static const RRGame *G;
#define HC 16
static int16_t hc_i[HC][HC], hc_j[HC][HC], hc_h[HC][HC];

static int16_t height_at(int16_t i, int16_t j)
{
    uint8_t a = (uint8_t)i & (HC - 1), b = (uint8_t)j & (HC - 1);
    if (hc_i[b][a] != i || hc_j[b][a] != j) {
        hc_i[b][a] = i;
        hc_j[b][a] = j;
        hc_h[b][a] = (int16_t)rr_height(G, (int32_t)i * CELL, (int32_t)j * CELL);
    }
    return hc_h[b][a];
}

/* the track's cross-section at z = zi * SEG: kerb, edge, middle, edge, kerb */
#define TC 16
static const int16_t off[5] = { -RR_TRACK_HW - 8, -RR_TRACK_HW, 0, RR_TRACK_HW, RR_TRACK_HW + 8 };
static int16_t tc_z[TC], tc_x[TC][5], tc_y[TC][5];

static uint8_t section(int16_t zi)
{
    uint8_t s = (uint8_t)zi & (TC - 1), k;
    if (tc_z[s] != zi) {
        int32_t z = (int32_t)zi * SEG, c = rr_centre(G, z);
        tc_z[s] = zi;
        for (k = 0; k < 5; k++) {
            tc_x[s][k] = (int16_t)(c + off[k]);
            /* (the valley's sides start at the edge: measured inside it) */
            tc_y[s][k] = (int16_t)rr_height(G, c + off[k] / 2, z) + 2;
        }
    }
    return s;
}

/* the gates near the bike: middle, pylon bases, banner height */
#define GC 4
static int16_t gc_k[GC], gc_c[GC], gc_h[GC][2], gc_y0[GC];

static uint8_t gate_info(int16_t k)
{
    uint8_t s = (uint8_t)k & (GC - 1);
    if (gc_k[s] != k) {
        int32_t gz = (int32_t)(k + 1) * RR_GATE_GAP, c = rr_centre(G, gz);
        gc_k[s] = k;
        gc_c[s] = (int16_t)c;
        gc_h[s][0] = (int16_t)rr_height(G, c - (RR_TRACK_HW - 10), gz);
        gc_h[s][1] = (int16_t)rr_height(G, c + (RR_TRACK_HW - 10), gz);
        gc_y0[s] = (int16_t)rr_height(G, c, gz) + 70;
    }
    return s;
}

void view_new_track(const RRGame *g)
{
    G = g;
    memset(hc_i, 0x80, sizeof(hc_i));          /* -32640: no cell */
    memset(tc_z, 0x80, sizeof(tc_z));
    memset(gc_k, 0x80, sizeof(gc_k));
}

/* ---- the scene ---- */

static int32_t floor_div(int32_t a, int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

static void camera(const RRGame *g, int32_t yaw)
{
    int32_t fx = a3_sin((uint16_t)yaw), fz = a3_cos((uint16_t)yaw), gy;
    int32_t x = g->x - fx * 150, z = g->z - fz * 150, y = g->y + ((int32_t)62 << 16);
    gy = rr_height(g, x >> 16, z >> 16) + 30;
    if ((y >> 16) < gy) y = gy << 16;
    camx = x >> 16; camy = y >> 16; camz = z >> 16;
    qx = (int16_t)((x >> 12) & 15); qy = (int16_t)((y >> 12) & 15); qz = (int16_t)((z >> 12) & 15);
    CY = (int16_t)(a3_cos((uint16_t)yaw) >> 4);
    SY = (int16_t)(a3_sin((uint16_t)yaw) >> 4);
    CP = (int16_t)(a3_cos(PITCH) >> 4);
    SP = (int16_t)(a3_sin(PITCH) >> 4);
    horizon = CYS - (int16_t)(((int32_t)FOVY * SP) / CP);
}

int16_t view_horizon(void)
{
    int16_t h = horizon >> 1;
    return h < 2 ? 2 : h > VIEW_H ? VIEW_H : h;
}

static void sky(int32_t yaw)
{
    static const uint8_t rw[12] = { 40, 45, 56, 61, 52, 51, 48, 52, 51, 52, 62, 61 };   /* the original's a3_hash sizes */
    static const uint8_t rh[12] = { 6, 23, 7, 14, 12, 19, 9, 10, 21, 10, 12, 14 };
    int16_t hz = view_horizon(), s[8];
    uint8_t k;
    /* the sky and the ground beyond the window */
    s[0] = 0; s[1] = 0; s[2] = VIEW_W; s[3] = 0; s[4] = VIEW_W; s[5] = hz; s[6] = 0; s[7] = hz;
    add_screen(s, 4, 32767, PEN_SKY);
    s[1] = s[3] = hz; s[5] = s[7] = VIEW_H;
    add_screen(s, 4, 32767, PEN_GROUND + 5);
    /* far ridges, drifting as the camera turns */
    for (k = 0; k < 12; k++) {
        int16_t x = (int16_t)((((int16_t)k * 61 - (int16_t)((uint16_t)yaw >> 6)) % 360 + 360) % 360) - 30;
        x = x * 4 / 5;                       /* port: the 320-wide screen's places, scaled */
        s[0] = x - rw[k] * 2 / 5; s[1] = hz;
        s[2] = x;                 s[3] = hz - rh[k] * 3 / 8;
        s[4] = x + rw[k] * 2 / 5; s[5] = hz;
        add_screen(s, 3, 32766, PEN_RIDGE);
    }
}

static void ground(const RRGame *g, int32_t yaw)
{
    static V3 pt[NV][NV];
    static int16_t hh[NV][NV];
    int32_t fx = a3_sin((uint16_t)yaw), fz = a3_cos((uint16_t)yaw);
    int32_t ax = (g->x >> 16) + ((fx * (WIN * CELL - CELL)) >> 16), az = (g->z >> 16) + ((fz * (WIN * CELL - CELL)) >> 16);
    int16_t ci = (int16_t)floor_div(ax, CELL) - WIN, cj = (int16_t)floor_div(az, CELL) - WIN;
    uint8_t i, j;
    for (j = 0; j < NV; j++)
        for (i = 0; i < NV; i++) {
            hh[j][i] = height_at(ci + i, cj + j);
            xform((int32_t)(ci + i) * CELL, hh[j][i], (int32_t)(cj + j) * CELL, &pt[j][i]);
        }
    for (j = 0; j < NV - 1; j++)
        for (i = 0; i < NV - 1; i++) {
            V3 q[4];
            int16_t ha = hh[j][i], hb = hh[j][i + 1], hc = hh[j + 1][i + 1], hd = hh[j + 1][i];
            int16_t slope = ((ha + hd) - (hb + hc)) + ((hc + hd) - (ha + hb)) / 2, pen;
            q[0] = pt[j][i]; q[1] = pt[j + 1][i]; q[2] = pt[j + 1][i + 1]; q[3] = pt[j][i + 1];   /* a d c b */
            if (q[0].z < NEAR && q[1].z < NEAR && q[2].z < NEAR && q[3].z < NEAR) continue;
            pen = 2 + slope / 24;                /* lit from the left and ahead */
            if (pen < 0) pen = 0;
            if (pen > 5) pen = 5;
            add(q, 4, (int16_t)(((q[0].z + q[1].z + q[2].z + q[3].z) >> 2) + 40 * U), (uint8_t)(PEN_GROUND + pen));
        }
}

static void track(const RRGame *g)
{
    int16_t z0 = (int16_t)(((g->z >> 16) / CELL - 2) * CELL), z1 = z0 + (WIN * 2 + 2) * CELL, z;
    if (z0 < -CELL) z0 = -CELL;
    if (z1 > RR_LEN + CELL) z1 = RR_LEN + CELL;
    for (z = z0; z < z1; z += SEG) {
        uint8_t a = section(z / SEG), b = section(z / SEG + 1), k, stripe = (uint8_t)((z / SEG) & 1);
        V3 p0[5], p1[5];
        for (k = 0; k < 5; k++) {
            xform(tc_x[a][k], tc_y[a][k], z, &p0[k]);
            xform(tc_x[b][k], tc_y[b][k], z + SEG, &p1[k]);
        }
        if (p1[2].z < NEAR) continue;
        for (k = 0; k < 4; k++) {
            V3 q[4];
            uint8_t pen = (k == 0 || k == 3) ? (stripe ? PEN_KERB : PEN_WHITE) : PEN_TRACK + ((k + stripe) & 1);
            q[0] = p0[k]; q[1] = p1[k]; q[2] = p1[k + 1]; q[3] = p0[k + 1];
            add(q, 4, (int16_t)(((p0[k].z + p1[k + 1].z) >> 1) + 20 * U), pen);
        }
    }
}

/* port: a pylon flat on to the camera: lit and shaded halves, and a cap */
static void pylon(int32_t x, int32_t y, int32_t z)
{
    V3 b, t, q[4];
    xform(x, y, z, &b);
    xform(x, y + 90, z, &t);
    if (b.z < NEAR || t.z < NEAR) return;
    q[0] = b; q[1] = t; q[2] = t; q[3] = b;
    q[0].x -= 9 * U; q[1].x -= 9 * U;
    add(q, 4, b.z, PEN_PYLON);
    q[0] = b; q[1] = t; q[2] = t; q[3] = b;
    q[2].x += 9 * U; q[3].x += 9 * U;
    add(q, 4, b.z, PEN_PYLON_DARK);
    q[0] = t; q[0].x -= 11 * U;
    q[1] = q[0]; q[1].y += 8 * U;
    q[2] = q[1]; q[2].x += 22 * U;
    q[3] = q[0]; q[3].x += 22 * U;
    add(q, 4, b.z - 2 * U, PEN_GREY);
}

static void gates(const RRGame *g)
{
    int16_t k;
    for (k = (int16_t)g->gate; k < RR_GATES && k < g->gate + 3; k++) {
        int32_t gz = (int32_t)(k + 1) * RR_GATE_GAP;
        uint8_t s = gate_info(k), last = k == RR_GATES - 1;
        int16_t c = gc_c[s];
        V3 q[4];
        if (gz - (g->z >> 16) > (WIN * 2 + 2) * CELL) break;
        pylon(c - RR_GATE_HW, gc_h[s][0], gz);
        pylon(c + RR_GATE_HW, gc_h[s][1], gz);
        if (k == g->gate) {                 /* the next gate's banner */
            int16_t y0 = gc_y0[s], m;
            xform(c - RR_GATE_HW, y0, gz, &q[0]);
            xform(c + RR_GATE_HW, y0, gz, &q[1]);
            xform(c + RR_GATE_HW, y0 + 16, gz, &q[2]);
            xform(c - RR_GATE_HW, y0 + 16, gz, &q[3]);
            if (q[0].z > NEAR) add(q, 4, q[0].z - 4 * U, last ? PEN_WHITE : PEN_BANNER);
            if (last)                        /* the finish: chequered */
                for (m = 0; m < 6; m++) {
                    int16_t xa = c - RR_GATE_HW + m * 30 + 2, ya = y0 + 2 + (m & 1) * 6;
                    xform(xa, ya, gz, &q[0]);
                    xform(xa + 13, ya, gz, &q[1]);
                    xform(xa + 13, ya + 6, gz, &q[2]);
                    xform(xa, ya + 6, gz, &q[3]);
                    if (q[0].z > NEAR) add(q, 4, q[0].z - 6 * U, PEN_BLACK);
                }
        }
    }
}

static void shadow(int32_t x, int32_t z)
{
    static const int16_t sx[6] = { 0, 14, 14, 0, -14, -14 }, sz[6] = { 24, 12, -12, -24, -12, 12 };
    int16_t y = (int16_t)rr_height(G, x, z) + 1;
    V3 p[6];
    uint8_t k;
    for (k = 0; k < 6; k++) xform(x + sx[k], y, z + sz[k], &p[k]);
    if (p[0].z > NEAR && p[3].z > NEAR) add(p, 6, ((p[0].z + p[3].z) >> 1) + 6 * U, PEN_SHADOW);
}

ViewBike view_bikes[RR_RIVALS + 1];

/* a bike as a sprite: where its origin lands, how big, and where a crest
   between it and the camera starts to hide it */
static void bike(uint8_t n, int32_t fx_, int32_t fy_, int32_t fz_, int32_t yaw, int16_t lean, uint8_t flame)
{
    ViewBike *b = &view_bikes[n];
    V3 p;
    int16_t sx, sy, k;
    int32_t x = fx_ >> 16, z = fz_ >> 16;
    xform(x, fy_ >> 16, z, &p);
    b->on = 0;
    if (p.z < NEAR + 2 * U) return;
    project(&p, &sx, &sy);
    b->on = 1;
    b->x = sx;
    b->y = (int16_t)(CYS - ((((int32_t)p.y * FOVY >> 8) * (((int32_t)1 << 20) / p.z)) >> 12));
    b->scale = (uint16_t)(((int32_t)FOVX * U << 8) / p.z);
    b->yaw = (int16_t)(yaw - (int32_t)0);
    b->lean = lean;
    b->colour = n;
    b->flame = flame;
    b->cut = 32767;
    /* a crest in front: the ground between the camera and the bike, at a
       few points, projected; the highest on the screen hides what is below */
    if (n)
        for (k = 1; k < 6; k++) {
            int32_t wx = camx + (x - camx) * k / 6, wz = camz + (z - camz) * k / 6;
            V3 c;
            int16_t cx, cy;
            xform(wx, height_at((int16_t)floor_div(wx + CELL / 2, CELL), (int16_t)floor_div(wz + CELL / 2, CELL)), wz, &c);
            if (c.z < NEAR) continue;
            project(&c, &cx, &cy);
            cy <<= 1;
            if (cy < b->cut) b->cut = cy;
        }
}

static void bikes(const RRGame *g, const ViewIn *in)
{
    uint8_t k;
    for (k = 0; k < RR_RIVALS; k++) {
        const Rival *r = &g->rv[k];
        int32_t x = r->x >> 16, z = r->z >> 16, dz = z - (g->z >> 16);
        view_bikes[k + 1].on = 0;
        if (r->finished || dz < -300 || dz > (WIN * 2) * CELL) continue;
        shadow(x, z);
        bike(k + 1, r->x, (int32_t)(rr_height(g, x, z) + RR_HOVER) << 16, r->z,
             a3_atan2(rr_centre(g, z + 60) - rr_centre(g, z), 60) - in->cam_yaw, 0, 0);
    }
    view_bikes[0].on = 0;
    if (in->show_player) {
        shadow(g->x >> 16, g->z >> 16);
        bike(0, g->x, g->y, g->z, g->yaw - in->cam_yaw, (int16_t)(-g->steer * 2200), (uint8_t)g->boosting);
    }
}

uint16_t view_build(const RRGame *g, const ViewIn *in)
{
    G = g;
    npoly = 0;
    nxy = 0;
    camera(g, in->cam_yaw);
    sky(in->cam_yaw);
    ground(g, in->cam_yaw);
    track(g);
    gates(g);
    bikes(g, in);
    return npoly;
}

/* far to near: a counting sort on key / 64 (four units), stable within a
   bucket (the order they were added: ground, then track, then the rest) */
static uint8_t bucket(int16_t k)
{
    if (k < 0) return 0;
    k >>= 6;
    return k > 255 ? 255 : (uint8_t)k;
}

void view_draw(ViewFill fill)
{
    static uint8_t count[256], start[256];
    uint16_t i;
    uint8_t b, n, k;
    memset(count, 0, sizeof(count));
    for (i = 0; i < npoly; i++) count[255 - bucket(poly[i].key)]++;
    for (k = 0, n = 0;; k++) { start[k] = n; n += count[k]; if (k == 255) break; }
    for (i = 0; i < npoly; i++) {
        b = 255 - bucket(poly[i].key);
        order[start[b]++] = (uint8_t)i;
    }
    for (i = 0; i < npoly; i++) {
        const Poly *p = &poly[order[i]];
        fill(&xy[p->at], p->n, p->pen);
    }
}
