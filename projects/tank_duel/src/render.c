/*
 * render.c - Tank Duel's picture from the turret, on the Next's Layer 2
 * (port of the original's render.c and the amiga3d pipeline it uses):
 * the sky and the floor, the grid, the walls, the cover, the enemy tanks,
 * shells, mortar rounds in flight with the rings where they will land,
 * explosions, the gun and the sight.
 *
 * As the original: camera space in fractions of a unit (no judder as the
 * tank creeps), the camera pitched 300/65536 of a turn down, everything
 * sorted far to near by its object's depth (a model's faces far first),
 * polygons clipped at the near plane. port:
 *   - camera space in eighths of a unit, not sixteenths (16-bit sums:
 *     the arena's diagonal is 2828 units)
 *   - 256 x 192 at 0.8 scale (fov 176, centre 128, 94): the original's
 *     320 x 240 view; the bottom 16 rows (the gun's foot, the bars) are
 *     the tilemap HUD's
 *   - a model's vertices are turned once per instance with the camera's
 *     yaw and pitch folded in, then offset by its centre: two multiplies
 *     a vertex instead of six (the sums are linear; the rounding differs
 *     by a sixteenth of a unit)
 *   - back faces are found from the projected polygon's winding
 *   - the floor grid's lines are a pixel wide (sdk l2line)
 *   - objects are sorted as a list of objects, not of polygons (the
 *     original's buckets keep each object's faces together anyway)
 * Polygons: sdk l2poly; the transform and the projection: xf.asm.
 */
#include <stdint.h>
#include "td.h"
#include "duel.h"
#include "models.h"
#include "l2poly.h"
#include "l2line.h"
#include "fixmath.h"
#include "nextgfx.h"

#define FOV   176
#define CX    128
#define CYC   94
#define U     8                         /* camera space: eighths of a unit (the arena's diagonal fits 16 bits) */
#define USH   3
#define NEARZ (12 * U)
#define CP14  16377                     /* cos, sin of the pitch (300/65536 turn) in Q14 */
#define SP14  471
#define EYE_Y 34

typedef struct { int16_t x, y, z; } v3;

uint8_t render_polys;
extern void lap(uint8_t k);

/* ------------------------------------------------------------------ trig */

/* sin of a 16-bit angle, Q14, interpolated in the SDK's 256-step table */
static int16_t rsin(uint16_t a)
{
    uint8_t i = (uint8_t)(a >> 8);
    int16_t s0 = fix_sin14(i), s1 = fix_sin14((uint8_t)(i + 1));
    return s0 + (int16_t)(mul16(s1 - s0, (uint8_t)a) >> 8);
}
#define rcos(a) rsin((uint16_t)(a) + 16384)

/* ------------------------------------------------------------------ camera */

static int16_t cam_x, cam_y, cam_z;     /* the eye, camera units */
static uint16_t cam_yaw;
static int16_t cy14, sy14;

/* xf.asm: the transform and the projection */
extern const void *xf_in;
extern void *xf_out;
extern int16_t xf_cam[3], xf_cy, xf_sy, xf_cp, xf_sp, xf_cx, xf_cyc;
extern uint8_t xf_fov;
extern uint16_t xf_recip[448];          /* (fov << 14) / z for z = 64..511 */
extern void xf_point(void);
extern void xf_proj(void);
extern int16_t xf_muldiv(int16_t a, int16_t b, int16_t c);   /* a * b / c */

/* a world point in camera units -> camera space (the original's cam_xform) */
static int16_t wpt[3];
static void xform(int16_t wx, int16_t wy, int16_t wz, v3 *o)
{
    wpt[0] = wx;
    wpt[1] = wy;
    wpt[2] = wz;
    xf_in = wpt;
    xf_out = o;
    xf_point();
}

/* port: the walls' and the grid's corners lie on a lattice, so they are
   built by adding camera-space steps to one transformed corner instead of
   transforming each of them (the steps are differences of transformed
   points: off by an eighth of a unit at most per step) */
static v3 corner, s200x, s200z, s400x, s400z, s2000x, s2000z, up70;

static void vadd(v3 *a, const v3 *b) { a->x += b->x; a->y += b->y; a->z += b->z; }

static void step_of(int16_t wx, int16_t wy, int16_t wz, v3 *o)
{
    xform(wx, wy, wz, o);
    o->x -= corner.x;
    o->y -= corner.y;
    o->z -= corner.z;
}

static void lattice_setup(void)
{
    const int16_t A = -TD_ARENA * U;
    xform(A, 0, A, &corner);
    step_of(A + 200 * U, 0, A, &s200x);
    step_of(A, 0, A + 200 * U, &s200z);
    step_of(A + 400 * U, 0, A, &s400x);
    step_of(A, 0, A + 400 * U, &s400z);
    step_of(-A, 0, A, &s2000x);
    step_of(A, 0, -A, &s2000z);
    step_of(A, 70 * U, A, &up70);
}

/* ------------------------------------------------------------------ polygons */

#define MAX_PV 10
static v3 clip_in[MAX_PV], clip_out[MAX_PV];
static int16_t sxy[MAX_PV * 2];

/* keep z >= NEARZ (Sutherland-Hodgman), interpolating from the inside end */
static uint8_t clip_near(const v3 *in, uint8_t n, v3 *out)
{
    uint8_t i, m = 0;
    for (i = 0; i < n; i++) {
        const v3 *a = &in[i], *b = &in[i + 1 == n ? 0 : i + 1];
        uint8_t ain = a->z >= NEARZ, bin = b->z >= NEARZ;
        if (ain && m < MAX_PV) out[m++] = *a;
        if (ain != bin && m < MAX_PV) {
            const v3 *p = ain ? a : b, *q = ain ? b : a;
            int16_t dz = q->z - p->z, t = NEARZ - p->z;
            out[m].x = p->x + xf_muldiv(q->x - p->x, t, dz);
            out[m].y = p->y + xf_muldiv(q->y - p->y, t, dz);
            out[m].z = NEARZ;
            m++;
        }
    }
    return m;
}

/* one Sutherland-Hodgman pass on screen coordinates: keep v >= lim (ge)
   or v <= lim on axis 0 (x) or 1 (y) */
static int16_t sxy2[MAX_PV * 2 + 8];
static uint8_t clip2(const int16_t *in, uint8_t n, int16_t *out, uint8_t axis, int16_t lim, uint8_t ge)
{
    uint8_t i, m = 0;
    for (i = 0; i < n; i++) {
        const int16_t *a = in + 2 * i, *b = in + 2 * (i + 1 == n ? 0 : i + 1);
        int16_t av = a[axis], bv = b[axis];
        uint8_t ain = ge ? av >= lim : av <= lim, bin = ge ? bv >= lim : bv <= lim;
        if (ain && m < MAX_PV + 4) { out[2 * m] = a[0]; out[2 * m + 1] = a[1]; m++; }
        if (ain != bin && m < MAX_PV + 4) {
            const int16_t *p = ain ? a : b, *q = ain ? b : a;
            int16_t pv = p[axis], other = p[axis ^ 1] + xf_muldiv(q[axis ^ 1] - p[axis ^ 1], lim - pv, q[axis] - pv);
            out[2 * m + axis] = lim;
            out[2 * m + (axis ^ 1)] = other;
            m++;
        }
    }
    return m;
}

static void fill(uint8_t n, uint8_t pen, uint8_t cull);

/* clip, project and fill; cull: 1 = only if it faces the camera */
static void poly(const v3 *pts, uint8_t n, uint8_t pen, uint8_t cull)
{
    uint8_t i, all_in = 1;
    const v3 *p = pts;
    int16_t *o = sxy;
    for (i = 0; i < n; i++) if (pts[i].z < NEARZ) all_in = 0;
    if (!all_in) {
        n = clip_near(pts, n, clip_out);
        if (n < 3) return;
        p = clip_out;
    }
    for (i = 0; i < n; i++, p++, o += 2) {
        xf_in = p;
        xf_out = o;
        xf_proj();
    }
    fill(n, pen, cull);
}

/* the projected polygon in sxy: cull, clip to the screen, fill */
static void fill(uint8_t n, uint8_t pen, uint8_t cull)
{
    uint8_t i;
    if (cull) {
        /* the projected winding (screen y down): faces wound outwards in
           the original's models come out clockwise when they face us */
        int32_t area = 0;
        int16_t *a = sxy;
        for (i = 0; i < n; i++) {
            int16_t *b = i + 1 == n ? sxy : a + 2;
            area += mul16(a[0], b[1]) - mul16(b[0], a[1]);
            a += 2;
        }
        if (area >= 0) return;
    }
    /* port: clipped to the screen's rows before filling (an edge from far
       off screen would be stepped through row by row) */
    {
        uint8_t i2, need = 0;
        int16_t *a = sxy;
        for (i2 = 0; i2 < n; i2++, a += 2)
            if (a[1] < -4000 || a[1] > 4192) need = 1;   /* (l2poly clips; only huge spans of rows cost) */
        if (need) {
            /* only the passes for the edges something crosses */
            static int16_t tmp[(MAX_PV + 8) * 2];
            int16_t *cur = sxy, *alt = tmp, *sw;
            uint8_t out = 0;
            a = sxy;
            for (i2 = 0; i2 < n; i2++, a += 2) {
                if (a[1] < 0) out |= 1;
                if (a[1] > 192) out |= 2;
                if (a[0] < -256) out |= 4;
                if (a[0] > 512) out |= 8;
            }
            if (out & 1) { n = clip2(cur, n, alt, 1, 0, 1); sw = cur; cur = alt; alt = sw == sxy ? sxy2 : sw; }
            if (n < 3) return;
            if (out & 2) { n = clip2(cur, n, alt, 1, 192, 0); sw = cur; cur = alt; alt = sw == sxy ? sxy2 : sw; }
            if (n < 3) return;
            if (out & 4) { n = clip2(cur, n, alt, 0, -256, 1); sw = cur; cur = alt; alt = sw == sxy ? sxy2 : sw; }
            if (n < 3) return;
            if (out & 8) { n = clip2(cur, n, alt, 0, 512, 0); cur = alt; }
            if (n < 3) return;
            l2p_poly(cur, n, pen);
            render_polys++;
            return;
        }
    }
    l2p_poly(sxy, n, pen);
    render_polys++;
}

/* ------------------------------------------------------------------ models */

static v3 mv[32];                       /* a model's vertices, camera space */
static int16_t mxy[32][2];              /* ... projected */
static uint8_t mdone[32];               /* 1: in mv, 2: in mxy too */

/* xf.asm: face culling and the model transform */
extern const md_face *md_faces;
extern uint8_t md_nf, md_vis[24], md_nvis;
extern int16_t md_ex, md_ez, md_cd, md_sd;
extern v3 md_c;
extern const md_vtx *md_vin;
extern v3 *md_vout;
extern void md_cull(void);
extern void md_xf(void);

/* model m at world (wx, wz) (camera units, on the ground) turned by yaw,
   centre c in camera space, body pens for MD_BODY faces.
   port: faces are culled in model space first (the eye turned into the
   model's frame against each face's plane: the same faces as the
   original's camera-space test), and only their vertices are turned and
   projected (xf.asm; the pitch's share of each vertex's height is worked
   out on the host, and its depth's share of z taken as the depth itself:
   cos 1.6 degrees) */
static void model(uint8_t m, int16_t wx, int16_t wz, uint16_t yaw, const uint8_t *body, const v3 *c)
{
    const md_model *md = &md_models[m];
    uint16_t d = yaw - cam_yaw;         /* turn relative to the camera */
    int16_t ct = rcos(yaw), st = rsin(yaw), dxw = cam_x - wx, dzw = cam_z - wz;
    uint8_t i, k, nvis = 0, front = 1;
    uint8_t vis[24];
    int16_t vdepth[24];
    /* the eye in the model's frame (units): x = X c - Z s, z = X s + Z c */
    md_ex = (int16_t)((mul16(dxw, ct) - mul16(dzw, st)) >> (14 + USH));
    md_ez = (int16_t)((mul16(dxw, st) + mul16(dzw, ct)) >> (14 + USH));
    md_faces = md->f;
    md_nf = md->nf;
    md_cull();
    md_cd = rcos(d);
    md_sd = rsin(d);
    md_c = *c;
    for (i = 0; i < md->nv; i++) mdone[i] = 0;
    for (k = 0; k < md_nvis; k++) {
        const md_face *f = &md->f[md_vis[k]];
        int16_t depth;
        for (i = 0; i < f->n; i++) {
            uint8_t j = f->idx[i];
            if (!mdone[j]) {
                md_vin = &md->xv[j];
                md_vout = &mv[j];
                md_xf();
                mdone[j] = 1;
                if (mv[j].z < NEARZ) front = 0;
            }
        }
        /* farthest first (several parts: culling alone doesn't order them);
           quarters: four depths overflow 16 bits */
        depth = (mv[f->idx[0]].z >> 2) + (mv[f->idx[1]].z >> 2) + (mv[f->idx[2]].z >> 2) +
                ((f->n == 4 ? mv[f->idx[3]].z : mv[f->idx[2]].z) >> 2);
        for (i = nvis; i > 0 && vdepth[i - 1] < depth; i--) {
            vis[i] = vis[i - 1];
            vdepth[i] = vdepth[i - 1];
        }
        vis[i] = md_vis[k];
        vdepth[i] = depth;
        nvis++;
    }
    for (i = 0; i < nvis; i++) {
        const md_face *f = &md->f[vis[i]];
        uint8_t col = f->col, j;
        if (col >= MD_BODY) col = body[col - MD_BODY];
        if (front) {
            for (j = 0; j < f->n; j++) {
                uint8_t q = f->idx[j];
                if (mdone[q] != 2) {
                    xf_in = &mv[q];
                    xf_out = mxy[q];
                    xf_proj();
                    mdone[q] = 2;
                }
                sxy[2 * j] = mxy[q][0];
                sxy[2 * j + 1] = mxy[q][1];
            }
            fill(f->n, col, 0);
            continue;
        }
        clip_in[0] = mv[f->idx[0]];
        clip_in[1] = mv[f->idx[1]];
        clip_in[2] = mv[f->idx[2]];
        if (f->n == 4) clip_in[3] = mv[f->idx[3]];
        poly(clip_in, f->n, col, 0);
    }
}

/* ------------------------------------------------------------------ the scene */

/* what to draw, sorted far to near */
enum { IT_WALL, IT_OBST, IT_TANK, IT_SHELL, IT_MORTAR, IT_RING, IT_BLAST };
typedef struct { int16_t depth; uint8_t kind, idx; v3 c; } item;
#define MAX_ITEMS 72
static item items[MAX_ITEMS];
static uint8_t n_items;

static void add(uint8_t kind, uint8_t idx, int16_t depth, const v3 *c)
{
    item *it;
    uint8_t i;
    if (n_items >= MAX_ITEMS) return;
    for (i = n_items; i > 0 && items[i - 1].depth < depth; i--) items[i] = items[i - 1];
    it = &items[i];
    it->kind = kind;
    it->idx = idx;
    it->depth = depth;
    if (c) it->c = *c;
    n_items++;
}

/* whether something of radius r (camera units) round camera point c may show */
static uint8_t in_view(const v3 *c, int16_t r)
{
    if (c->z < NEARZ - r) return 0;
    /* x / z beyond 128 / 176 is off the screen */
    if (mul16(c->x, FOV) > mul16(c->z, CX) + mul16(r, 220)) return 0;
    if (mul16(-c->x, FOV) > mul16(c->z, CX) + mul16(r, 220)) return 0;
    return 1;
}

#define TOU(f) ((int16_t)((f) >> (16 - USH)))   /* 16.16 world -> camera units */

/* the walls' corners: 4 sides x 6 lattice points, at the foot and at 70 */
static v3 wall_pt[4][6][2];

static void walls_setup(void)
{
    uint8_t side, j;
    v3 a, b;
    for (side = 0; side < 4; side++) {
        /* side 0: z = +1000, x along; 1: x = +1000, z along; 2: z = -1000; 3: x = -1000 */
        const v3 *along = side & 1 ? &s400z : &s400x;
        a = corner;
        if (side == 0) vadd(&a, &s2000z);
        else if (side == 1) vadd(&a, &s2000x);
        for (j = 0; j < 6; j++) {
            wall_pt[side][j][0] = a;
            b = a;
            vadd(&b, &up70);
            wall_pt[side][j][1] = b;
            vadd(&a, along);
        }
    }
}

static void wall_quad(uint8_t k, v3 *q)
{
    uint8_t side = k / 5, seg = k % 5;
    q[0] = wall_pt[side][seg][0];
    q[1] = wall_pt[side][seg + 1][0];
    q[2] = wall_pt[side][seg + 1][1];
    q[3] = wall_pt[side][seg][1];
}

static v3 quad[4];

static void collect(void)
{
    const TDGame *g = &game;
    uint8_t k;
    v3 c;
    n_items = 0;
    for (k = 0; k < 20; k++) {
        wall_quad(k, quad);
        if (quad[0].z < NEARZ && quad[1].z < NEARZ && quad[2].z < NEARZ && quad[3].z < NEARZ) continue;
        add(IT_WALL, k, (quad[0].z >> 1) + (quad[2].z >> 1), 0);
    }
    for (k = 0; k < g->nobs; k++) {
        const Obstacle *o = &g->ob[k];
        if (o->kind == OB_CRATE && o->hp <= 0) continue;
        xform(TOU(o->x), 0, TOU(o->z), &c);
        if (in_view(&c, 60 * U)) add(IT_OBST, k, c.z, &c);
    }
    for (k = 1; k < g->ntanks; k++) {
        const Tank *t = &g->tk[k];
        if (!t->alive) continue;
        xform(TOU(t->x), 0, TOU(t->z), &c);
        if (in_view(&c, 45 * U)) add(IT_TANK, k, c.z, &c);
    }
    for (k = 0; k < TD_MAX_SHELLS; k++) {
        const Shell *s = &g->sh[k];
        if (!s->alive) continue;
        xform(TOU(s->x), 20 * U, TOU(s->z), &c);
        if (c.z >= NEARZ + 5 * U) add(IT_SHELL, k, c.z - 4 * U, &c);
    }
    for (k = 0; k < TD_MAX_MORTARS; k++) {
        const Mortar *m = &g->mo[k];
        int16_t f, x, z, y;
        if (!m->alive) continue;
        f = (int16_t)((int32_t)m->t * 256 / m->flight);          /* 0..256 of the way */
        x = (int16_t)(m->x0 >> 16) + (int16_t)(((int32_t)((m->tx - m->x0) >> 16) * f) >> 8);
        z = (int16_t)(m->z0 >> 16) + (int16_t)(((int32_t)((m->tz - m->z0) >> 16) * f) >> 8);
        y = 20 + (int16_t)((int32_t)4 * 280 * f * (256 - f) / 65536);
        xform(x * U, y * U, z * U, &c);
        if (c.z >= NEARZ + 7 * U) add(IT_MORTAR, k, c.z - 4 * U, &c);
        xform(TOU(m->tx), 0, TOU(m->tz), &c);
        if (in_view(&c, TD_SPLASH_R * U)) add(IT_RING, k, c.z + 30 * U, &c);
    }
    for (k = 0; k < MAX_BLASTS; k++) {
        const Blast *b = &du.blast[k];
        if (b->t <= 0) continue;
        xform(b->x * U, (b->y + (10 - b->t) * 2) * U, b->z * U, &c);
        if (c.z >= NEARZ + 40 * U) add(IT_BLAST, k, c.z - 8 * U, &c);
    }
}

/* a square facing the camera, half size r (units) */
static void spot(const v3 *c, int16_t r, uint8_t pen)
{
    r *= U;
    if (c->z < NEARZ + r) return;
    quad[0].x = c->x - r; quad[0].y = c->y - r; quad[0].z = c->z;
    quad[1].x = c->x + r; quad[1].y = c->y - r; quad[1].z = c->z;
    quad[2].x = c->x + r; quad[2].y = c->y + r; quad[2].z = c->z;
    quad[3].x = c->x - r; quad[3].y = c->y + r; quad[3].z = c->z;
    poly(quad, 4, pen, 0);
}

/* an explosion: a ragged star facing the camera, turning as it fades */
static void burst(const v3 *c, int16_t r, uint8_t pen, int8_t t)
{
    uint8_t k;
    r *= U;
    if (c->z < NEARZ + r) return;
    for (k = 0; k < 8; k++) {
        uint16_t a = (uint16_t)k * 8192 + (uint16_t)t * 1500;
        int16_t rr = (k & 1) ? r / 2 : r;
        clip_in[k].x = c->x + (int16_t)(mul16(rsin(a), rr) >> 14);
        clip_in[k].y = c->y + (int16_t)(mul16(rcos(a), rr) >> 14);
        clip_in[k].z = c->z;
    }
    poly(clip_in, 8, pen, 0);
}

/* the ring on the ground where a mortar round will land */
static void ring(int16_t x, int16_t z, int16_t r, uint8_t pen)
{
    uint8_t k;
    for (k = 0; k < 12; k++) {
        uint16_t a0 = (uint16_t)k * 5461, a1 = a0 + 5461;
        int16_t s0 = rsin(a0), c0 = rcos(a0), s1 = rsin(a1), c1 = rcos(a1);
        xform(x + (int16_t)(mul16(s0, r * U) >> 14), U, z + (int16_t)(mul16(c0, r * U) >> 14), &quad[0]);
        xform(x + (int16_t)(mul16(s1, r * U) >> 14), U, z + (int16_t)(mul16(c1, r * U) >> 14), &quad[1]);
        xform(x + (int16_t)(mul16(s1, (r - 8) * U) >> 14), U, z + (int16_t)(mul16(c1, (r - 8) * U) >> 14), &quad[2]);
        xform(x + (int16_t)(mul16(s0, (r - 8) * U) >> 14), U, z + (int16_t)(mul16(c0, (r - 8) * U) >> 14), &quad[3]);
        poly(quad, 4, pen, 0);
    }
}

/* one grid line: the segment a-b (camera space) clipped at the near plane,
   projected, drawn 1 pixel wide */
static void grid_line(v3 *a, v3 *b)
{
    int16_t xy[4];
    if (a->z < NEARZ && b->z < NEARZ) return;
    if (a->z < NEARZ || b->z < NEARZ) {
        v3 *p = a->z < NEARZ ? b : a, *q = a->z < NEARZ ? a : b;   /* p inside */
        int16_t dz = q->z - p->z, t = NEARZ - p->z;
        q->x = p->x + xf_muldiv(q->x - p->x, t, dz);
        q->y = p->y + xf_muldiv(q->y - p->y, t, dz);
        q->z = NEARZ;
    }
    xf_in = a; xf_out = xy; xf_proj();
    xf_in = b; xf_out = xy + 2; xf_proj();
    l2l_line(xy[0], xy[1], xy[2], xy[3], PEN_GRID);
    render_polys++;
}

/* port: the grid's lines are drawn 1 pixel wide (the original: strips 6
   units wide, mostly a pixel or less at this size anyway) */
static void floor_grid(void)
{
    uint8_t i;
    v3 ax, az, b;
    ax = corner;
    az = corner;
    for (i = 0; i <= 10; i++) {
        quad[0] = ax;                       /* x = -1000 + 200 i, z from -1000 to 1000 */
        b = ax; vadd(&b, &s2000z); quad[1] = b;
        grid_line(&quad[0], &quad[1]);
        quad[0] = az;                       /* z = -1000 + 200 i, x from -1000 to 1000 */
        b = az; vadd(&b, &s2000x); quad[1] = b;
        grid_line(&quad[0], &quad[1]);
        vadd(&ax, &s200x);
        vadd(&az, &s200z);
    }
}

static const uint8_t crate_body[2][2] = {{15, 14}, {13, 14}};
static const uint8_t no_body[2] = {3, 3};

static void draw_items(void)
{
    const TDGame *g = &game;
    uint8_t i;
    for (i = 0; i < n_items; i++) {
        const item *it = &items[i];
        switch (it->kind) {
        case IT_WALL:
            wall_quad(it->idx, quad);
            poly(quad, 4, PEN_WALL + (it->idx % 5 & 1), 0);
            break;
        case IT_OBST: {
            const Obstacle *o = &g->ob[it->idx];
            model(o->kind == OB_PILLAR ? MD_PILLAR : o->kind == OB_PYRAMID ? MD_PYRAMID : MD_CRATE,
                  TOU(o->x), TOU(o->z), 0, crate_body[o->hp >= 2], &it->c);
            break;
        }
        case IT_TANK: {
            const Tank *t = &g->tk[it->idx];
            model(MD_SCOUT + t->type - TK_SCOUT, TOU(t->x), TOU(t->z), (uint16_t)t->yaw, no_body, &it->c);
            break;
        }
        case IT_SHELL:
            spot(&it->c, 5, g->sh[it->idx].owner ? PEN_ESHELL : PEN_SHELL);
            break;
        case IT_MORTAR:
            spot(&it->c, 7, PEN_ESHELL);
            break;
        case IT_RING: {
            const Mortar *m = &g->mo[it->idx];
            ring(TOU(m->tx), TOU(m->tz), TD_SPLASH_R, (g->tick & 4) ? PEN_RING : PEN_RED);
            break;
        }
        case IT_BLAST: {
            const Blast *b = &du.blast[it->idx];
            burst(&it->c, (int16_t)b->size * (4 + b->t) / 14, b->t > 7 ? PEN_YELLOW : b->t > 3 ? PEN_FIRE : PEN_RED, b->t);
            break;
        }
        }
    }
}

/* the gun at the bottom and the sight (the original's gun(), at 0.8) */
static const int16_t gun_xy[8] = {117, 165, 139, 165, 149, 204, 107, 204};

static void gun(void)
{
    int16_t xy[8];
    uint8_t i, kick = game.tk[0].reload > TD_RELOAD - 4 ? 5 : 0;
    for (i = 0; i < 8; i++) xy[i] = gun_xy[i] + ((i & 1) && i < 4 ? kick : 0);
    l2p_poly(xy, 4, 3);
    l2p_rect(165 + kick, 169 + kick, 123, 134, 0);
    l2p_rect(86, 91, 127, 129, PEN_GREEN);
    l2p_rect(98, 103, 127, 129, PEN_GREEN);
    l2p_rect(93, 95, 118, 123, PEN_GREEN);
    l2p_rect(93, 95, 133, 138, PEN_GREEN);
}

static void hurt_edge(void)
{
    l2p_rect(0, 3, 0, 256, PEN_RED);
    l2p_rect(189, 192, 0, 256, PEN_RED);
    l2p_rect(0, 192, 0, 3, PEN_RED);
    l2p_rect(0, 192, 253, 256, PEN_RED);
}

/* ------------------------------------------------------------------ the frame */

void render_init(void)
{
    xf_cp = CP14;
    xf_sp = SP14;
    xf_fov = FOV;
    {
        uint16_t z;
        for (z = 64; z < 512; z++) xf_recip[z - 64] = (uint16_t)(((uint32_t)FOV << 14) / z);
    }
    xf_cx = CX;
    xf_cyc = CYC;
}

void render_scene(void)
{
    const Tank *p = &game.tk[0];
    int16_t hz;
    cam_x = TOU(p->x);
    cam_z = TOU(p->z);
    cam_y = EYE_Y * U;
    cam_yaw = (uint16_t)p->yaw;
    cy14 = rcos(cam_yaw);
    sy14 = rsin(cam_yaw);
    xf_cam[0] = cam_x;
    xf_cam[1] = cam_y;
    xf_cam[2] = cam_z;
    xf_cy = cy14;
    xf_sy = sy14;
    render_polys = 0;
    hz = CYC - (int16_t)(mul16(FOV, SP14) / CP14);
    l2p_begin();
    l2p_rect(0, hz, 0, 256, PEN_SKY);
    l2p_rect(hz, 192, 0, 256, PEN_FLOOR);
    lattice_setup();
    walls_setup();
    floor_grid();
    lap(1);
    collect();
    lap(2);
    draw_items();
    lap(3);
    if (du.state != DS_TITLE) {
        gun();
        if (du.hurt_timer > 0) hurt_edge();
    }
    l2p_end();
}
