/*
 * render.c - the canyon (the original's render.c canyon()): slabs 100 long
 * out to 1300 ahead (the classic Amiga's view_ahead), each with its floor
 * and river, the two rock walls sloping out as they rise, and a spire on
 * the rim now and then; painted far to near with sdk l2poly.
 *
 * port: each slab edge's points go through the engine's camera rotation
 * once (shared by the slabs either side; with 16-bit deltas the same values
 * as cam_xform), then a reciprocal scale (sdk persp) onto 256 x 192; the
 * slab across the near plane is clipped there. The floor is one bank quad
 * with the river over it (the original: three quads). Drawn 96 rows high:
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


uint16_t render_polys;

typedef struct { int16_t x, y, z; } P3;

static Cam cam;
int16_t cy4, sy4, cp4, sp4;             /* the camera's trig >> 4 (cam_xform's factors; view.asm) */
extern void xform16(int16_t dx, int16_t dy, int16_t dz, P3 *o);   /* view.asm */

/* an edge: L top, L base, river L, river R, R base, R top (camera space, screen) */
typedef struct {
    int32_t s;                          /* where */
    int16_t h;
    P3 c[6];
    int16_t px[6], py[6];
    uint8_t front;                      /* all six in front of the near plane */
    const void *p;                      /* its Place */
} Edge;
static Edge edge[NE];

#define xform(wx, wy, wz, o) xform16((int16_t)((wx) - cam.x), (int16_t)((wy) - cam.y), (int16_t)((wz) - cam.z), o)

static void project(const P3 *p, int16_t *x, int16_t *y)
{
    uint16_t s = persp_scale((uint16_t)p->z);
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
    int16_t c = p->c, w = p->w, h = p->h;
    uint8_t k;
    e->s = s;
    e->h = h;
    e->p = p;
    xform(c - w - 70, h, s, &e->c[0]);
    xform(c - w, 0, s, &e->c[1]);
    xform(c - 30, 0, s, &e->c[2]);
    xform(c + 30, 0, s, &e->c[3]);
    xform(c + w, 0, s, &e->c[4]);
    xform(c + w + 70, h, s, &e->c[5]);
    e->front = 1;
    for (k = 0; k < 6; k++) {
        if (e->c[k].z < NEAR_Z) { e->front = 0; continue; }
        project(&e->c[k], &e->px[k], &e->py[k]);
    }
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
    P3 apex;
    const P3 *v[3];
    const Place *pa = a->p, *pb = b->p;
    int32_t x = ((int32_t)pa->c + pb->c + side * ((int32_t)pa->w + pb->w + 140)) / 2;
    xform(x + side * 20, (a->h + b->h) / 2 + 90, a->s + SEG / 2, &apex);
    v[0] = &a->c[k]; v[1] = &b->c[k]; v[2] = &apex;
    poly3(v, 3, PEN_SPIRE);
}

void render_init(void)
{
    uint8_t k;
    for (k = 0; k < 32; k++) place[k].s = -1;
    persp_fov(SX(200));                 /* the original's FOV, scaled across (render.h) */
}

void render_scene(const CRGame *g)
{
    int32_t s, s0 = (g->cam_s / SEG) * SEG - SEG, s1 = g->cam_s + AHEAD;
    int16_t hz;
    uint8_t n = 0, k;
    cr_camera(g, &cam);
    cy4 = (int16_t)(cam.cy >> 4); sy4 = (int16_t)(cam.sy >> 4);
    cp4 = (int16_t)(cam.cp >> 4); sp4 = (int16_t)(cam.sp >> 4);
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
    l2p_rect(hz, VIEW_ROWS + 1, 0, VIEW_W - 1, PEN_WALL + 1);
    for (k = n - 1; k-- > 0; ) {
        const Edge *a = &edge[k], *b = &edge[k + 1];
        uint8_t tone = (uint8_t)((b->s - a->s > SEG ? a->s / (2 * SEG) : a->s / SEG) & 1);  /* (pairs alternate too) */
        if (b->c[1].z < NEAR_Z && b->c[4].z < NEAR_Z) continue;        /* behind */
        quad(a, b, 1, 4, PEN_BANK + tone);
        quad(a, b, 2, 3, PEN_RIVER);
        quad(a, b, 1, 0, PEN_WALL + 2 * tone);
        quad(a, b, 4, 5, PEN_WALL + 2 * tone + 1);
        if (((const Place *)a->p)->spire & 1) spire(a, b, -1);
        if (((const Place *)a->p)->spire & 2) spire(a, b, 1);
    }
    l2p_end();
}
