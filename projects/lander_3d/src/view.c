// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * view.c - Lander 3D's picture (the original's render.c: camera(), sky(),
 * ground(), pads(), craft_draw()) for the Next: the ground in a window of
 * the height grid round the craft, each cell shaded by its slope, the
 * pads, the lander tilted by its pitch and roll with its exhaust and its
 * shadow, the stars. It works out the screen positions; vfill.c draws.
 *
 * World units for the engine: 16 to the metre (the simulation is fixed
 * point metres; >> 12 turns one into the other).
 *
 * port: the original's camera, cells, pens and model, drawn by vfill.c's
 * near-to-far columns instead of a sorted polygon list (vfill.h). The
 * lander's faces are culled by their turned normals and sorted by their
 * depth as the original's a3_model does. The view is the original's at
 * 0.8 across (256 pixels for 320) and drawn at half height (92 rows for
 * the 184 lines it is shown on; the copper doubles them).
 *
 * In bank 21 at 0x0000.
 */
#include <string.h>
#include "fix16.h"
#include "fixmath.h"
#include "ld.h"
#include "view.h"
#include "vfill.h"
#include "pens.h"
#include "vproj.h"
#include "vobj.h"

#define U(f)       ((int32_t)((f) >> 12))      /* fix metres -> engine units */
#define NEAR_Z     12
#define FOVX       160                          /* the original's 200, at 0.8 */
#define FOVY16     (80 * 16)                    /* rows (half of 0.8 x 200), in 1/16 */
#define CX         128
#define CY16       830                          /* (11 + 0.8 x 116) / 2 rows, x 16 */

/* ---------------------------------------------------------- projection */
/* a world point (units) into camera space (vobj.asm): yaw 0, the pitch */
static void xform(int32_t wx, int32_t wy, int32_t wz, V3s *o)
{
    vo_cam((int16_t)(wx - vcam.x), (int16_t)(wy - vcam.y), (int16_t)(wz - vcam.z), o);
}

/* a polygon in camera space clipped to z >= NEAR_Z (the original's
   clip_near_at, from the inside end), projected (vobj.asm), into the list */
#define MAXV 10
static void poly(V3s *in, uint8_t n, uint8_t pen, uint8_t snap)
{
    V3s out[MAXV], *src = in;
    VfVtx sv[MAXV];
    uint8_t i, m = 0;
    for (i = 0; i < n && in[i].z >= NEAR_Z; i++)
        ;
    if (i < n) {                         /* a point nearer than the near plane: clip */
        for (i = 0; i < n; i++) {
            V3s *a = &in[i], *b = &in[i + 1 == n ? 0 : i + 1];
            uint8_t ain = a->z >= NEAR_Z, bin = b->z >= NEAR_Z;
            if (ain && m < MAXV) { out[m].x = a->x; out[m].y = a->y; out[m].z = a->z; m++; }
            if (ain != bin && m < MAXV) {
                V3s *p = ain ? a : b, *q = ain ? b : a;
                int16_t dz = q->z - p->z, t = NEAR_Z - p->z;
                out[m].x = p->x + (int16_t)((int32_t)(q->x - p->x) * t / dz);
                out[m].y = p->y + (int16_t)((int32_t)(q->y - p->y) * t / dz);
                out[m].z = NEAR_Z;
                m++;
            }
        }
        if (m < 3) return;
        src = out;
        n = m;
    }
    vo_scr_n(src, sv, n);
    if (vf_add_poly(sv, n, pen, snap)) view_polys++;
}

/* ---------------------------------------------------------- the ground */
static int32_t z_line0, cell_u;          /* line 0's world z, a cell (units) */
static int16_t j_line0;                  /* line 0's grid row */

static void ground(const Level *L)
{
    const Terrain *T = &L->T;
    int16_t ci, cj;
    int32_t half_u = U(T->half), cy;

    cell_u = U(T->cell);
    /* centred a little ahead of the craft (the camera looks along +z) */
    ci = (int16_t)FINT(fdiv(L->c.p[0] + T->half, T->cell));
    cj = (int16_t)FINT(fdiv(L->c.p[2] + T->half, T->cell)) + GW / 2;
    j_line0 = cj - GW;
    z_line0 = -half_u + (int32_t)j_line0 * cell_u;
    vf_nlines = NLINE;
    /* vproj.asm: the grid points and the cells' pens */
    vp_dx0 = (int16_t)(-half_u + (int32_t)(ci - GW) * cell_u - vcam.x);
    vp_dz0 = (int16_t)(z_line0 - vcam.z);
    vp_cell = (int16_t)cell_u;
    cy = vcam.y;
    vp_camy = (int16_t)(cy > 30000 ? 30000 : cy < -30000 ? -30000 : cy);
    vp_cp = vcam.cp;
    vp_sp = vcam.sp;
    vp_i0 = ci - GW;
    vp_j0 = j_line0;
    vp_n = (uint8_t)T->n;
    vp_page = T->page;
    vp_rpp = T->rpp;
    vp_rowb = T->rowb;
    vp_hb12 = (int16_t)(T->hbase >> 12);
    vp_hbf = (uint16_t)(T->hbase & 0xFFF);
    vp_cdiv = (uint16_t)(cell_u / 3 + 1);
    vp_pen0 = PEN_GROUND;
    vp_project();
    ter_done();
}

/* the strip a world z (units) lies in: vf_ybuf before it = what is nearer */
static uint8_t strip_of(int32_t wz)
{
    int32_t s = (wz - z_line0) / cell_u;
    if (wz < z_line0) return 0;
    return s > NLINE - 1 ? NLINE - 1 : (uint8_t)s;
}

/* render.c octagon(): 8 points round (cx, cz) at the ground's height +
   lift, at the angles 22.5 + 45 k degrees. port: the middle into camera
   space, then each point as the middle plus its turned offset (the
   octagon is level: (ox, 0, oz) turns to (ox, oz sin, oz cos)); the
   offsets are +-r sin 22.5 and +-r cos 22.5, four products a frame */
static void octagon(int32_t cx, int32_t cy, int32_t cz, int16_t r, uint8_t pen, uint8_t snap)
{
    static const int8_t sx[8] = { 1, 2, 2, 1, -1, -2, -2, -1 }, sz[8] = { 2, 1, -1, -2, -2, -1, 1, 2 };
    V3s p[8], m;
    int16_t a = (int16_t)(mul16(r, 6270) >> 14), b = (int16_t)(mul16(r, 15137) >> 14);   /* r sin, r cos 22.5 (Q14) */
    int16_t as = (int16_t)(mul16(a, vcam.sp) >> 12), ac = (int16_t)(mul16(a, vcam.cp) >> 12);
    int16_t bs = (int16_t)(mul16(b, vcam.sp) >> 12), bc = (int16_t)(mul16(b, vcam.cp) >> 12);
    uint8_t k;
    xform(cx, cy, cz, &m);
    for (k = 0; k < 8; k++) {
        int8_t i = sx[k], j = sz[k];
        p[k].x = m.x + (i == 1 ? a : i == 2 ? b : i == -1 ? -a : -b);
        p[k].y = m.y + (j == 1 ? as : j == 2 ? bs : j == -1 ? -as : -bs);
        p[k].z = m.z + (j == 1 ? ac : j == 2 ? bc : j == -1 ? -ac : -bc);
    }
    if (p[0].z < NEAR_Z && p[4].z < NEAR_Z) return;
    poly(p, 8, pen, snap);
}

/* ---------------------------------------------------------- the lander */
/* render.c render_init_models(): the descent stage in gold foil, the ascent
   stage on top, four legs and feet, the engine bell underneath (units: 16 to
   the metre). Each box's faces take its lit pen facing -x, +y or +z and its
   shade facing +x, -y or -z (the original's sun at (-1, 2, 1), mdl_face()).
   port: drawn as boxes: their corners from three turned edges, the faces
   facing the camera chosen per axis from the direction to the craft (it is
   small against its distance), the boxes far to near (the original sorts
   every face) */
typedef struct { int8_t lo[3], hi[3]; uint8_t lit, shade; } Box;
static const Box boxes[7] = {
    { { -24, -14, -24 }, { 24, 8, 24 }, PEN_GOLD, PEN_GOLD + 1 },
    { { -16, 8, -14 }, { 16, 30, 18 }, PEN_STAGE, PEN_STAGE + 1 },
    { { -4, 30, -4 }, { 4, 36, 4 }, PEN_STAGE + 1, PEN_STAGE + 1 },
    { { 30, -32, 30 }, { 34, -6, 34 }, PEN_LEGS, PEN_LEGS },
    { { -34, -32, 30 }, { -30, -6, 34 }, PEN_LEGS, PEN_LEGS },
    { { 30, -32, -34 }, { 34, -6, -30 }, PEN_LEGS, PEN_LEGS },
    { { -34, -32, -34 }, { -30, -6, -30 }, PEN_LEGS, PEN_LEGS },
};
/* the bell: mdl_pyramid(-12, 12, -14, -12, 12, -26), base corners then the apex */
static const int8_t bell_v[5][3] = { { -12, -14, -12 }, { 12, -14, -12 }, { 12, -14, 12 }, { -12, -14, 12 }, { 0, -26, 0 } };
/* its faces' outward normals (base corner k, k + 1, apex), scaled */
static const int8_t bell_n[4][3] = { { 0, -24, -24 }, { 24, -24, 0 }, { 0, -24, 24 }, { -24, -24, 0 } };

void view_init(void) {}

/* the 4 corners of a box's face on axis a (0 x, 1 y, 2 z) at its hi (1) or
   lo (0) end, as corner indices (bit 0 x, 1 y, 2 z), round the face */
static const uint8_t face_c[3][4] = { { 0, 2, 6, 4 }, { 0, 1, 5, 4 }, { 0, 1, 3, 2 } };

static void craft(const Level *L, uint8_t snap, uint8_t frame)
{
    const Craft *c = &L->c;
    int32_t px = U(c->p[0]), py = U(c->p[1]), pz = U(c->p[2]);
    int16_t cp = (int16_t)(a3_cos((uint16_t)a3_angle(c->pitch)) >> 4), sp = (int16_t)(a3_sin((uint16_t)a3_angle(c->pitch)) >> 4);
    int16_t cr = (int16_t)(a3_cos((uint16_t)a3_angle(c->roll)) >> 4), sr = (int16_t)(a3_sin((uint16_t)a3_angle(c->roll)) >> 4);
    int16_t R[9], K[9], bz[8];
    uint8_t vis[3], order[8], nb, i, k, a;
    V3s ctr, corner[8], term[3][2];
    VfVtx sv[8];
    /* R = Rx(pitch) Rz(roll); K = the camera's pitch x R (Q12) */
    R[0] = cr; R[1] = -sr; R[2] = 0;
    R[3] = (int16_t)(mul16(cp, sr) >> 12); R[4] = (int16_t)(mul16(cp, cr) >> 12); R[5] = -sp;
    R[6] = (int16_t)(mul16(sp, sr) >> 12); R[7] = (int16_t)(mul16(sp, cr) >> 12); R[8] = cp;
    for (k = 0; k < 3; k++) {
        K[k] = R[k];
        K[3 + k] = (int16_t)((mul16(R[3 + k], vcam.cp) + mul16(R[6 + k], vcam.sp)) >> 12);
        K[6 + k] = (int16_t)((mul16(R[6 + k], vcam.cp) - mul16(R[3 + k], vcam.sp)) >> 12);
    }
    xform(px, py, pz, &ctr);
    if (ctr.z < NEAR_Z) return;
    /* which end of each axis faces the camera: the + face if its turned
       normal points back along the direction to the craft */
    for (a = 0; a < 3; a++)
        vis[a] = mul16(K[a], ctr.x) + mul16(K[3 + a], ctr.y) + mul16(K[6 + a], ctr.z) < 0;
    /* the boxes far to near (by their middles' depth) */
    nb = 0;
    for (i = 0; i < 7; i++) {
        const Box *b = &boxes[i];
        int16_t mx = (b->lo[0] + b->hi[0]) / 2, my = (b->lo[1] + b->hi[1]) / 2, mz = (b->lo[2] + b->hi[2]) / 2;
        int16_t z = (int16_t)((mul16(K[6], mx) + mul16(K[7], my) + mul16(K[8], mz)) >> 12);
        for (k = nb; k > 0 && bz[k - 1] < z; k--) { bz[k] = bz[k - 1]; order[k] = order[k - 1]; }
        bz[k] = z;
        order[k] = i;
        nb++;
    }
    /* the bell first (it is under the descent stage, seen from above) */
    {
        V3s bv[5];
        for (k = 0; k < 5; k++) {
            int16_t x = bell_v[k][0], y = bell_v[k][1], z = bell_v[k][2];
            bv[k].x = ctr.x + (int16_t)((mul16(K[0], x) + mul16(K[1], y) + mul16(K[2], z)) >> 12);
            bv[k].y = ctr.y + (int16_t)((mul16(K[3], x) + mul16(K[4], y) + mul16(K[5], z)) >> 12);
            bv[k].z = ctr.z + (int16_t)((mul16(K[6], x) + mul16(K[7], y) + mul16(K[8], z)) >> 12);
        }
        for (k = 0; k < 4; k++) {
            int16_t nx = bell_n[k][0], ny = bell_n[k][1], nz = bell_n[k][2];
            int16_t wx = (int16_t)((mul16(nx, K[0]) + mul16(ny, K[1]) + mul16(nz, K[2])) >> 8);
            int16_t wy = (int16_t)((mul16(nx, K[3]) + mul16(ny, K[4]) + mul16(nz, K[5])) >> 8);
            int16_t wz = (int16_t)((mul16(nx, K[6]) + mul16(ny, K[7]) + mul16(nz, K[8])) >> 8);
            if (mul16(wx, ctr.x) + mul16(wy, ctr.y) + mul16(wz, ctr.z) < 0) {
                V3s q[3];
                q[0] = bv[k]; q[1] = bv[(k + 1) & 3]; q[2] = bv[4];
                poly(q, 3, PEN_LEGS, snap);
            }
        }
    }
    for (i = 0; i < nb; i++) {
        const Box *b = &boxes[order[i]];
        /* the six turned edges: term[axis][end] = K's column x the end's coordinate */
        for (a = 0; a < 3; a++)
            for (k = 0; k < 2; k++) {
                int16_t v = k ? b->hi[a] : b->lo[a];
                term[a][k].x = (int16_t)(mul16(K[a], v) >> 12);
                term[a][k].y = (int16_t)(mul16(K[3 + a], v) >> 12);
                term[a][k].z = (int16_t)(mul16(K[6 + a], v) >> 12);
            }
        for (k = 0; k < 8; k++) {
            const V3s *tx = &term[0][k & 1], *ty = &term[1][(k >> 1) & 1], *tz = &term[2][k >> 2];
            corner[k].x = ctr.x + tx->x + ty->x + tz->x;
            corner[k].y = ctr.y + tx->y + ty->y + tz->y;
            corner[k].z = ctr.z + tx->z + ty->z + tz->z;
        }
        for (k = 0; k < 8 && corner[k].z >= NEAR_Z; k++)
            ;
        if (k == 8) vo_scr_n(corner, sv, 8);
        for (a = 0; a < 3; a++) {
            uint8_t end = vis[a], bit = (uint8_t)(1 << a), f;
            /* lit facing -x, +y, +z */
            uint8_t pen = (a == 0 ? !end : end) ? b->lit : b->shade;
            if (k == 8) {
                VfVtx fv[4];
                for (f = 0; f < 4; f++) fv[f] = sv[face_c[a][f] | (end ? bit : 0)];
                if (vf_add_poly(fv, 4, pen, snap)) view_polys++;
            } else {                         /* near the camera: clipped */
                V3s q[4];
                for (f = 0; f < 4; f++) q[f] = corner[face_c[a][f] | (end ? bit : 0)];
                poly(q, 4, pen, snap);
            }
        }
    }
    /* the exhaust: a flickering cone along -up, as long as the throttle */
    if (c->throttle > 0 && !L->outcome) {
        V3s q[3];
        const fix *u = vcam.up;
        int32_t len = 30 + ((c->throttle * 70) >> 16) + (int32_t)((frame * 7) % 9);
        int32_t by = py - 24;
        xform(px - 8, by, pz, &q[0]);
        xform(px + 8, by, pz, &q[1]);
        xform(px - ((u[0] * len) >> 16), by - ((u[1] * len) >> 16), pz - ((u[2] * len) >> 16), &q[2]);
        poly(q, 3, (frame & 1) ? PEN_FLAME : PEN_FLAME + 1, snap);
    }
}

/* ---------------------------------------------------------- the sky */
static uint32_t a3_hash(uint32_t x)      /* the engine's stateless scramble */
{
    x ^= x >> 16;
    x *= 0x7feb352dUL;
    x ^= x >> 15;
    x *= 0x846ca68bUL;
    x ^= x >> 16;
    return x;
}

/* render.c sky(): 70 stars at fixed places (none on Mars) */
static uint8_t stars_made;
static void stars(uint8_t world)
{
    uint8_t k, n = 0;
    if (world == W_MARS) { vf_nstars = 0; stars_made = 0; return; }
    if (stars_made) return;
    stars_made = 1;
    for (k = 0; k < VF_NSTARS; k++) {
        uint32_t h = a3_hash((uint32_t)k * 7919u);
        uint16_t x = (uint16_t)(h % 320), y = (uint16_t)((h >> 12) % 120);
        vf_star_x[n] = (uint8_t)(x * 4 / 5);
        vf_star_y[n] = (uint8_t)((11 + y * 4 / 5) / 2);
        vf_star_pen[n] = (k & 3) ? PEN_STAR : 1;
        n++;
    }
    vf_nstars = n;
}

void view_draw(const Level *L, uint8_t frame)
{
    uint8_t k, sc;
    int16_t hz;
    view_polys = 0;
    vf_clear_polys();
    /* the horizon: cy - fov tan(pitch), on this view's rows */
    hz = (int16_t)((CY16 - (int32_t)FOVY16 * vcam.sp / vcam.cp) >> 4);
    vf_hz = hz < 0 ? 0 : hz > VROWS ? VROWS : (uint8_t)hz;
    vf_sky = L->world == W_MARS ? PEN_SKY : 0;
    vf_plain = PEN_GROUND + 4;
    stars(L->world);
    ground(L);
    /* the pads, each over what is behind its near edge */
    for (k = 0; k < 3; k++) {
        const Pad *p = &L->T.pads[k];
        int32_t x = U(p->x), z = U(p->z), r = U(p->r), y = U(p->y);
        vf_snap_at[k] = strip_of(z - r);
        octagon(x, y + 2, z, (int16_t)r, PEN_RING + k, k);
        octagon(x, y + 3, z, (int16_t)(r * 3 / 4), PEN_PAD, k);
    }
    /* the shadow, straight down, and the lander */
    sc = 3;
    vf_snap_at[sc] = strip_of(U(L->c.p[2]));
    octagon(U(L->c.p[0]), vcam.shadow_y + 3, U(L->c.p[2]), 40, PEN_SHADOW, sc);
    craft(L, sc, frame);
    vf_draw();
}
