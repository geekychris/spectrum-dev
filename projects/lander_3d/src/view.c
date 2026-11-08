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
    /* port: the lines wholly below the view (their highest point under its
       bottom edge, a line nearer the camera than the first that isn't) are
       left out, and on each line the points beyond the view's sides (for
       the lowest ground, the widest): no picture there */
    {
        uint8_t r, n = (uint8_t)T->n;
        int16_t dz = vp_dz0, hi, lo, dy, yp, zp;
        vp_r0 = 0;
        for (r = 0; r < NLINE; r++, dz += vp_cell) {
            int16_t j = j_line0 + r;
            vp_xlim[r] = 32000;
            if (j < 0 || j >= n) { if (vp_r0 == r) vp_r0 = r + 1; continue; }
            ter_range(T, (uint8_t)j, &hi, &lo);
            dy = hi - vp_camy;
            yp = (int16_t)((mul16(dy, vp_cp) + mul16(dz, vp_sp)) >> 12);
            zp = (int16_t)((mul16(dz, vp_cp) - mul16(dy, vp_sp)) >> 12);
            if (vp_r0 == r && (zp <= 0 || mul16(yp, 80) + mul16(zp, 42) < 0)) vp_r0 = r + 1;
            dy = lo - vp_camy;
            zp = (int16_t)((mul16(dz, vp_cp) - mul16(dy, vp_sp)) >> 12);
            if (zp > 0 && zp < 20000) vp_xlim[r] = zp + zp / 5 + vp_cell;
        }
        if (vp_r0) vp_r0--;                  /* the line before the first seen: the wedges under it */
    }
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
    V3s p[8], m;
    VfVtx sv[8];
    int16_t off[6];
    off[0] = (int16_t)(mul16(r, 6270) >> 14);       /* r sin, r cos 22.5 (Q14) */
    off[1] = (int16_t)(mul16(r, 15137) >> 14);
    off[2] = (int16_t)(mul16(off[0], vcam.sp) >> 12);
    off[3] = (int16_t)(mul16(off[0], vcam.cp) >> 12);
    off[4] = (int16_t)(mul16(off[1], vcam.sp) >> 12);
    off[5] = (int16_t)(mul16(off[1], vcam.cp) >> 12);
    xform(cx, cy, cz, &m);
    if (!vo_oct(&m, off, sv, p)) {           /* (vobj.asm) */
        if (vf_add_poly(sv, 8, pen, snap)) view_polys++;
        return;
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

void view_init(void) {}

/* the 4 corners of a box's face on axis a (0 x, 1 y, 2 z) at its hi (1) or
   lo (0) end, as corner indices (bit 0 x, 1 y, 2 z), round the face */
static const uint8_t face_c[3][4] = { { 0, 2, 6, 4 }, { 0, 1, 5, 4 }, { 0, 1, 3, 2 } };

static VfVtx bsv[7][8];                  /* the boxes' corners on the screen, in camera space */
static V3s bcv[7][8];
static V3s bell_c[5];
static void craft(const Level *L, uint8_t snap, uint8_t frame)
{
    const Craft *c = &L->c;
    int32_t px = U(c->p[0]), py = U(c->p[1]), pz = U(c->p[2]);
    int16_t cp = (int16_t)(a3_cos((uint16_t)a3_angle(c->pitch)) >> 4), sp = (int16_t)(a3_sin((uint16_t)a3_angle(c->pitch)) >> 4);
    int16_t cr = (int16_t)(a3_cos((uint16_t)a3_angle(c->roll)) >> 4), sr = (int16_t)(a3_sin((uint16_t)a3_angle(c->roll)) >> 4);
    int16_t R[9], K[9], bz[7], d[3];
    uint8_t near[7], order[7], i, k, a, f;
    V3s ctr;
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
    /* each axis's turned direction against the direction to the craft: a
       face whose normal n has n . d < 0 faces the camera (it is small
       against its distance: the same for every face) */
    for (a = 0; a < 3; a++)
        d[a] = (int16_t)((mul16(K[a], ctr.x) + mul16(K[3 + a], ctr.y) + mul16(K[6 + a], ctr.z)) >> 8);
    /* the boxes' corners (vobj.asm), then far to near by their depth */
    for (i = 0; i < 7; i++) {
        int16_t z;
        near[i] = vo_box(&boxes[i].lo[0], K, &ctr, bsv[i], bcv[i]);
        z = bcv[i][0].z + bcv[i][7].z;
        for (k = i; k > 0 && bz[k - 1] < z; k--) { bz[k] = bz[k - 1]; order[k] = order[k - 1]; }
        bz[k] = z;
        order[k] = i;
    }
    /* the bell first (under the descent stage); its faces' normals are
       (0,-1,-1) (1,-1,0) (0,-1,1) (-1,-1,0) */
    vo_model(&bell_v[0][0], 5, K, &ctr, bell_c);
    {
        int16_t f0 = -d[1] - d[2], f1 = d[0] - d[1], f2 = d[2] - d[1], f3 = -d[0] - d[1];
        int16_t fd[4];
        fd[0] = f0; fd[1] = f1; fd[2] = f2; fd[3] = f3;
        for (k = 0; k < 4; k++)
            if (fd[k] < 0) {
                V3s q[3];
                uint8_t k1 = (k + 1) & 3;
                q[0].x = bell_c[k].x; q[0].y = bell_c[k].y; q[0].z = bell_c[k].z;
                q[1].x = bell_c[k1].x; q[1].y = bell_c[k1].y; q[1].z = bell_c[k1].z;
                q[2].x = bell_c[4].x; q[2].y = bell_c[4].y; q[2].z = bell_c[4].z;
                poly(q, 3, PEN_LEGS, snap);
            }
    }
    for (i = 0; i < 7; i++) {
        uint8_t b = order[i], nc = (d[0] < 0 ? 1 : 0) | (d[1] < 0 ? 2 : 0) | (d[2] < 0 ? 4 : 0);   /* the nearest corner */
        const Box *bx = &boxes[b];
        const VfVtx *sv = bsv[b];
        const V3s *cv = bcv[b];
        if (bx->lit == bx->shade) {
            /* port: one pen: the three faces' outline, a hexagon (the
               corners one and two steps from the nearest), the same pixels */
            static const uint8_t hexo[6] = { 1, 3, 2, 6, 4, 5 };
            if (!near[b]) {
                VfVtx fv[6];
                for (f = 0; f < 6; f++) { const VfVtx *q = &sv[nc ^ hexo[f]]; fv[f].x = q->x; fv[f].y = q->y; }
                if (vf_add_poly(fv, 6, bx->lit, snap)) view_polys++;
            } else {
                V3s q[6];
                for (f = 0; f < 6; f++) { const V3s *r = &cv[nc ^ hexo[f]]; q[f].x = r->x; q[f].y = r->y; q[f].z = r->z; }
                poly(q, 6, bx->lit, snap);
            }
            continue;
        }
        for (a = 0; a < 3; a++) {
            uint8_t end = (nc >> a) & 1, bit = (uint8_t)(1 << a);
            uint8_t pen = (a == 0 ? !end : end) ? bx->lit : bx->shade;          /* lit facing -x, +y, +z */
            const uint8_t *fc = face_c[a];
            if (!near[b]) {
                VfVtx fv[4];
                for (f = 0; f < 4; f++) { const VfVtx *q = &sv[fc[f] | (end ? bit : 0)]; fv[f].x = q->x; fv[f].y = q->y; }
                if (vf_add_poly(fv, 4, pen, snap)) view_polys++;
            } else {                         /* near the camera: clipped */
                V3s q[4];
                for (f = 0; f < 4; f++) { const V3s *r = &cv[fc[f] | (end ? bit : 0)]; q[f].x = r->x; q[f].y = r->y; q[f].z = r->z; }
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

/* render.c compass(): an arrow at the top pointing to the nearest pad, in
   its ring's colour (and for Mars's wind one at the top right in the HUD's
   grey: port: the original draws two lines). On the view's coordinates:
   the original's (x, y) at (0.8 x, (11 + 0.8 y) / 2) */
static void arrow(int16_t cx, int16_t cy, uint16_t a, uint8_t pen)
{
    static const int8_t r[3] = { 12, 7, 7 };
    static const uint16_t da[3] = { 0, 26000, (uint16_t)-26000 };
    VfVtx v[3];
    uint8_t k;
    for (k = 0; k < 3; k++) {
        uint16_t b = a + da[k];
        int16_t x = cx + (int16_t)((a3_sin(b) * r[k]) >> 16), y = cy - (int16_t)((a3_cos(b) * r[k]) >> 16);
        v[k].x = (int16_t)(x * 4 / 5);
        v[k].y = (uint16_t)((11 + y * 4 / 5 + 2 * VF_OFF) * 128);
    }
    vf_add_poly(v, 3, pen, VF_SNAPS);
}

static void compass(void)
{
    arrow(160, 18, hv.pad_dir, PEN_RING + hv.pad);
    if (hv.wind) arrow(290, 18, hv.wind_dir, PEN_HUD);
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
    if (view_compass) compass();
    vf_draw();
}
