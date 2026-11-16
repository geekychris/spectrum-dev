/*
 * render.c - the canyon (the original's render.c canyon()): slabs 100 long
 * out to 1300 ahead (the classic Amiga's view_ahead), each with its floor
 * and river, the two rock walls sloping out as they rise, and a spire on
 * the rim now and then; painted far to near with sdk l2poly.
 *
 * port: each slab edge's points go through the engine's camera rotation
 * once (shared by the slabs either side; with 16-bit deltas the same values
 * as cam_xform), then a reciprocal scale (sdk persp) onto 256 x 192; the
 * slab across the near plane is clipped there. The ground is filled in the
 * banks' first tone, every other slab's bank is a quad in the second, and
 * the river is a quad over them (the original: three quads a slab). Drawn 96 rows high:
 * main.c's copper shows each row twice.
 */
#include <stdint.h>
#include "cr.h"
#include "render.h"
#include "l2poly.h"
#include "persp.h"

#define SEG 100
#define AHEAD 1300                      /* the classic Amiga's view_ahead */
#define NE (AHEAD / SEG + 3)            /* edges drawn at most */
#define PAIR_AHEAD 600                  /* port: slabs in pairs from here on */




typedef struct { int16_t x, y, z; } P3;

A3Cam render_cam;                       /* this frame's camera (sprites.c too) */
#define cam render_cam
extern int16_t cy4, sy4, cp4, sp4;      /* the camera's trig >> 4 (cam_xform's factors; in view.asm) */
extern void xform16(int16_t dx, int16_t dy, int16_t dz, P3 *o);   /* view.asm */
extern uint16_t zscale(uint16_t z) __z88dk_fastcall;            /* view.asm: persp_scale by ztab */
uint16_t ztab[1024];                    /* persp_scale(4 k + 2), for zscale */

/* an edge: L top, L base, river L, river R, R base, R top (camera space,
   screen); view.asm's edge6 knows this layout */
typedef struct {
    P3 c[6];
    int16_t px[6], py[6];
    uint8_t front;                      /* all six in front of the near plane */
    uint8_t seg;                        /* s / SEG (low bits: tones) */
    int32_t s;                          /* where */
    int16_t h;
    const void *p;                      /* its Place */
} Edge;
extern void edge6f(int16_t c, int16_t w, int16_t dyb, int16_t dyt, int16_t dz, Edge *e);   /* view.asm */
extern void edge_frame(void);
static Edge edge[NE];

#define xform(wx, wy, wz, o) xform16((int16_t)((wx) - cam.x), (int16_t)((wy) - cam.y), (int16_t)((wz) - cam.z), o)

static void project(const P3 *p, int16_t *x, int16_t *y)
{
    uint16_t s = zscale((uint16_t)p->z);
    *x = persp_x1(p->x, 0, s, VIEW_W / 2);
    *y = persp_y1(p->y, 0, (s >> 2) + (s >> 3), VIEW_ROWS / 2);    /* (3/4 of the across scale, half height) */
}

/* an edge's place in the world: the same every time (port: worked out once,
   when it comes into view; the original's hash and sines are slow here) */
typedef struct { int32_t s; int16_t c, w, h; uint8_t spire; } Place;
static Place place[32];

static const Place *place_of(int32_t s)
{
    Place *p = &place[(uint8_t)(s / SEG) & 31];
    int8_t side;
    if (p->s == s) return p;
    p->s = s;
    p->c = (int16_t)cr_path_x(s);
    p->w = (int16_t)cr_half_width(s);
    p->h = 250 + (int16_t)(a3_hash((uint32_t)s * 7u) % 90);
    p->spire = 0;
    for (side = -1; side <= 1; side += 2)
        if ((a3_hash((uint32_t)s * 13u + (uint32_t)side) & 3) == 0) p->spire |= side < 0 ? 1 : 2;
    return p;
}

static void make_edge(Edge *e, int32_t s)
{
    const Place *p = place_of(s);
    e->s = s;
    e->seg = (uint8_t)(s / SEG);
    e->h = p->h;
    e->p = p;
    edge6f(p->c - (int16_t)cam.x, p->w, (int16_t)-cam.y, p->h - (int16_t)cam.y, (int16_t)(s - cam.z), e);
}

/* a polygon of camera-space points, clipped at the near plane */
static void poly3(const P3 *const *v, uint8_t n, uint8_t col)
{
    static int16_t xy[16];
    P3 q;
    uint8_t i, m = 0;
    for (i = 0; i < n && m < 8; i++) {
        const P3 *a = v[i], *b = v[i + 1 == n ? 0 : i + 1];
        if (a->z >= NEAR_Z) { project(a, &xy[m * 2], &xy[m * 2 + 1]); m++; }
        if ((a->z >= NEAR_Z) != (b->z >= NEAR_Z)) {
            /* where a-b crosses z = NEAR_Z */
            int32_t t = ((int32_t)(NEAR_Z - a->z) << 12) / (b->z - a->z);
            q.x = a->x + (int16_t)(((int32_t)(b->x - a->x) * t) >> 12);
            q.y = a->y + (int16_t)(((int32_t)(b->y - a->y) * t) >> 12);
            q.z = NEAR_Z;
            project(&q, &xy[m * 2], &xy[m * 2 + 1]);
            m++;
        }
    }
    if (m >= 3) { l2p_poly(xy, m, col); render_polys++; }
}

/* a quad of edge points: a0, a1 on the near edge, b1, b0 on the far one */
static void quad(const Edge *a, const Edge *b, uint8_t i, uint8_t j, uint8_t col)
{
    if (a->front && b->front) {
        static int16_t xy[8];
        xy[0] = a->px[i]; xy[1] = a->py[i];
        xy[2] = a->px[j]; xy[3] = a->py[j];
        xy[4] = b->px[j]; xy[5] = b->py[j];
        xy[6] = b->px[i]; xy[7] = b->py[i];
        l2p_poly(xy, 4, col);
        render_polys++;
    } else {
        const P3 *v[4];
        v[0] = &a->c[i]; v[1] = &a->c[j]; v[2] = &b->c[j]; v[3] = &b->c[i];
        poly3(v, 4, col);
    }
}

static void spire(const Edge *a, const Edge *b, int8_t side)
{
    uint8_t k = side < 0 ? 0 : 5;
    const Place *pa = a->p, *pb = b->p;
    int32_t x = ((int32_t)pa->c + pb->c + side * ((int32_t)pa->w + pb->w + 140)) / 2;
    P3 apex;
    xform(x + side * 20, (a->h + b->h) / 2 + 90, a->s + (b->s - a->s) / 2, &apex);
    if (a->front && b->front && apex.z >= NEAR_Z) {
        /* the rim's two points are projected with their edges */
        static int16_t xy[6];
        xy[0] = a->px[k]; xy[1] = a->py[k];
        xy[2] = b->px[k]; xy[3] = b->py[k];
        project(&apex, &xy[4], &xy[5]);
        l2p_poly(xy, 3, PEN_SPIRE);
        render_polys++;
    } else {
        const P3 *v[3];
        v[0] = &a->c[k]; v[1] = &b->c[k]; v[2] = &apex;
        poly3(v, 3, PEN_SPIRE);
    }
}

void render_init(void)
{
    uint16_t k;
    for (k = 0; k < 32; k++) place[k].s = -1;
    persp_fov(SX(200));                 /* the original's FOV, scaled across (render.h) */
    for (k = 64; k < 1024; k++) ztab[k] = persp_scale(k * 4 + 2);
}

void render_scene(const CRGame *g)
{
    int32_t s, s0 = (g->cam_s / SEG) * SEG - SEG, s1 = g->cam_s + AHEAD;
    int16_t hz;
    uint8_t n = 0, k, pass;
    cr_camera(g, &cam);
    cy4 = (int16_t)(cam.cy >> 4); sy4 = (int16_t)(cam.sy >> 4);
    cp4 = (int16_t)(cam.cp >> 4); sp4 = (int16_t)(cam.sp >> 4);
    edge_frame();
    hz = SY(cam_horizon(&cam)) / 2;
    if (hz < 1) hz = 1;
    if (hz > VIEW_ROWS - 1) hz = VIEW_ROWS - 1;
    /* the edges (slabs from s0 to s1); port: from PAIR_AHEAD on, every other
       one: far slabs drawn in pairs */
    s = s0;
    while (s < s1 + SEG && n < NE) {
        make_edge(&edge[n++], s);
        s += SEG;
        if (s - g->cam_s > PAIR_AHEAD && (uint8_t)(s / SEG) & 1) s += SEG;     /* (pairs start on even hundreds) */
    }

    render_polys = 0;
    l2p_begin();
    l2p_top = 0;
    l2p_bot = VIEW_ROWS + 1;                  /* (the last line shows row 96) */
    l2p_rect(0, hz, 0, VIEW_W - 1, PEN_SKY);
    l2p_rect(hz, VIEW_ROWS + 1, 0, VIEW_W - 1, PEN_BANK);      /* port: the bank's first tone (the original: a wall's) */
    /* port: the floor first (the banks, then the river: one slab's bank
       quad would leave slivers over the next one's river), then the walls and
       spires; each far to near */
    for (pass = 0; pass < 3; pass++)
        for (k = n - 1; k-- > 0; ) {
            const Edge *a = &edge[k], *b = &edge[k + 1];
            uint8_t tone = (b->seg - a->seg > 1 ? a->seg >> 1 : a->seg) & 1;     /* (pairs alternate too) */
            if (b->c[1].z < NEAR_Z && b->c[4].z < NEAR_Z) continue;        /* behind */
            if (pass == 0) { if (tone) quad(a, b, 1, 4, PEN_BANK + 1); }    /* (tone 0: the ground's fill) */
            else if (pass == 1) quad(a, b, 2, 3, PEN_RIVER);
            else {
                quad(a, b, 1, 0, PEN_WALL + 2 * tone);
                quad(a, b, 4, 5, PEN_WALL + 2 * tone + 1);
                if (((const Place *)a->p)->spire & 1) spire(a, b, -1);
                if (((const Place *)a->p)->spire & 2) spire(a, b, 1);
            }
        }
    if (render_tracer) {
        /* the tracer, from the gun to the sight (the original's hud) */
        static int16_t t[6];
        t[0] = VIEW_W / 2 - 3; t[1] = VIEW_ROWS;
        t[2] = VIEW_W / 2 + 3; t[3] = VIEW_ROWS;
        t[4] = SX(g->sx); t[5] = SY(g->sy) / 2;
        l2p_poly(t, 3, render_tracer);
    }
    l2p_end();
}
