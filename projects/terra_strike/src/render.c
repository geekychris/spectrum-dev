/*
 * Terra Strike - the 3D view on the Next (the original's render.c).
 *
 * As the original: every frame the 15 x 15 terrain tiles around the view
 * and every model are put into camera space, culled, clipped against the
 * near plane and dropped into depth buckets, which are drawn far to near
 * (a painter's algorithm, as Zarch did); models are keyed to the depth of
 * the tile they stand over. The camera is the original's (camera_follow).
 *
 * port:
 * - The screen is 256 x 192 Layer 2 (double buffered); the original's
 *   320 x 212 view at 0.8: 256 x 170, focal length 136. It is drawn 85 rows
 *   high and the copper shows each row twice (half the rows to fill).
 * - The terrain's camera-space points are sums: a grid point is the first
 *   one plus i steps across and j steps along (the camera's rotation is
 *   linear), so only its height costs multiplies.
 * - Projection: 1/z from a table (render_init), then MULs (fixmath mul16).
 * - Back faces are culled by their winding on screen.
 * - Shots, shells and particles are hardware sprites (sprites.c), drawn
 *   over the polygons instead of depth-sorted with them.
 * - The polygons are filled by the SDK's l2poly (Z80N assembly).
 */
#include <stdint.h>
#include <string.h>
#include "ts.h"
#include "models.h"
#include "models_data.h"
#include "render.h"
#include "points.h"
#include "fixmath.h"
#include "l2poly.h"

extern void prof(uint8_t phase);   /* main.c: timings (temporary) */

Cam cam;

/* ------------------------------------------------------------ camera (the original's) */

void camera_follow(Cam *c, const World *w)
{
    int32_t tx, ty, tz, dist, up, g;
    if (w->state == GS_TITLE) {
        pad_centre(w, &tx, &tz);
        ty = w->pad_h;
        c->yaw = (w->frame >> 1) & 255;
        c->pitch = 18;
        dist = 1300;
        up = 700;
    } else {
        const Craft *p = &w->p;
        int16_t d = ((p->yaw - c->yaw + 128) & 255) - 128;
        tx = p->x >> FBITS; ty = p->y >> FBITS; tz = p->z >> FBITS;
        c->yaw = (c->yaw + (d / 3 ? d / 3 : d)) & 255;
        c->pitch = 15;
        dist = 560;
        up = 250;
    }
    c->x = ts_wrap(tx - ((TS_SIN(c->yaw) * dist) >> 16));
    c->z = ts_wrap(tz - ((TS_COS(c->yaw) * dist) >> 16));
    c->y = ty + up;
    g = ground_height(w, c->x, c->z) + 60;
    if (c->y < g) c->y = g;
    c->cy = TS_COS(c->yaw);
    c->sy = TS_SIN(c->yaw);
    c->cp = TS_COS(c->pitch);
    c->sp = TS_SIN(c->pitch);
}

/* ------------------------------------------------------------ projection */

/* FOCAL * 4096 / z: z < 1024 by z / 4, to FAR_Z by z / 32, beyond (the
   far corners of tiles partly in range) by a division */
#define RFAR_N (FAR_Z / 32 + 2)
uint16_t rnear[256], rfar[RFAR_N];
static int16_t ccy, csy, ccp, csp;      /* the camera's trig, 4096 = 1 (the original's >> 4) */

static uint16_t recip(int16_t z)
{
    if (z < 1024) return rnear[(uint16_t)z >> 2];
    if (((uint16_t)z >> 5) < RFAR_N) return rfar[(uint16_t)z >> 5];
    return (uint16_t)(((uint32_t)R_FOCAL << 12) / (uint16_t)z);
}

static int16_t clampc(int32_t v)
{
    return v > 16000 ? 16000 : v < -16000 ? -16000 : (int16_t)v;
}

/* a camera-space point (z >= NEAR_Z) to the screen; y in drawn (half) rows */
static void project(int16_t x, int16_t y, int16_t z, int16_t *out)
{
    uint16_t s = recip(z);
    out[0] = clampc(R_CX + (mul16(x, (int16_t)s) >> 12));
    out[1] = clampc((R_CY - (mul16(y, (int16_t)s) >> 12)) >> 1);
}

/* the shortest wrapped offset a - b on the 16384-unit world, 16-bit */
static int16_t wrap16(int32_t a, int32_t b)
{
    int16_t d = (int16_t)(((uint16_t)a - (uint16_t)b) & WORLD_MASK);
    return d >= WORLD_SIZE / 2 ? d - WORLD_SIZE : d;
}

/* tgrid.asm: a model's vertices (here also: one point, the camera's own
   rotation) */
extern const int16_t *xf_src;
extern uint8_t xf_n, xf_flat, xf_wide;
extern int16_t xf_m00, xf_m01, xf_m10, xf_m11, xf_cx, xf_cy, xf_cz;
extern int16_t mvx[MDL_MAXV], mvy[MDL_MAXV], mvz[MDL_MAXV], msx[MDL_MAXV], msy[MDL_MAXV];
extern void xf_model(void);

/* the original's cam_xform: one point into camera space (and onto the
   screen: msx[0], msy[0] when z >= NEAR_Z) */
static void xform(int32_t wx, int32_t wy, int32_t wz, V3 *out)
{
    static int16_t d[3];
    d[0] = wrap16(wx, cam.x);
    d[1] = (int16_t)(wy - cam.y);
    d[2] = wrap16(wz, cam.z);
    xf_m00 = ccy; xf_m01 = -csy; xf_m10 = csy; xf_m11 = ccy;
    xf_cx = xf_cy = xf_cz = 0;
    xf_flat = 0;
    xf_n = 1;
    xf_src = d;
    xf_wide = 1;
    xf_model();
    xf_wide = 0;
    out->x = mvx[0];
    out->y = mvy[0];
    out->z = mvz[0];
}

/* ------------------------------------------------------------ the draw list */

/* polygons in an 8K page mapped at 0x4000 (render.h): the next one in its
   bucket, n, colour, then n (x, y) pairs; buckets as lists of offsets */
#define NBUCKET 232                       /* depth / 16 (FAR_Z 3600) */
#define bhead ((uint16_t *)DRAW_ARENA)                        /* the arena page: buckets, */
#define btail ((uint16_t *)(DRAW_ARENA + 2 * NBUCKET))
#define tdepth ((int16_t *)(DRAW_ARENA + 4 * NBUCKET))        /* the tiles' depths, */
#define gs_x ((int16_t *)(DRAW_ARENA + 0x580))                /* the grid on screen (tgrid.asm), */
#define gs_y ((int16_t *)(DRAW_ARENA + 0x780))         /* graw: 0x980 (tgrid.asm), */
#define POLY_FIRST 0xB80                                      /* then the polygons */
uint16_t arena_used;                                          /* (tgrid.asm too) */
static uint8_t *const arena = (uint8_t *)DRAW_ARENA;

static int16_t *add_poly_slot(int16_t depth, uint8_t col, uint8_t n)
{
    uint8_t *p;
    uint16_t need = 4 + 4 * (uint16_t)n, b;
    if (arena_used + need > DRAW_ARENA_SIZE) {
        render_stats.dropped++;
        return 0;
    }
    b = depth <= 0 ? 0 : (uint16_t)depth >> 4;
    if (b >= NBUCKET) b = NBUCKET - 1;
    p = arena + arena_used;
    *(uint16_t *)p = 0;                   /* appended: a bucket draws in the order added */
    if (btail[b]) *(uint16_t *)(arena + btail[b] - 1) = arena_used + 1;
    else bhead[b] = arena_used + 1;       /* offsets + 1 (0 = none) */
    btail[b] = arena_used + 1;
    p[2] = n;
    p[3] = col;
    arena_used += need;
    render_stats.polys++;
    return (int16_t *)(p + 4);
}

/* clip a camera-space polygon at the near plane (the original's clip_near,
   32-bit), project it and add it */
static void add_clipped(int16_t depth, uint8_t col, const V3 *in, uint8_t n)
{
    V3 out[MAX_PV];
    uint8_t i, m = 0;
    int16_t *xy;
    for (i = 0; i < n; i++) {
        const V3 *a = &in[i], *b = &in[i + 1 == n ? 0 : i + 1];
        uint8_t ain = a->z >= NEAR_Z, bin = b->z >= NEAR_Z;
        if (ain && m < MAX_PV) out[m++] = *a;
        if (ain != bin && m < MAX_PV) {
            const V3 *p = ain ? a : b, *q = ain ? b : a;
            /* port: the fraction once (Q12), a MUL per coordinate */
            int16_t f = (int16_t)(((int32_t)(NEAR_Z - p->z) << 12) / (q->z - p->z));
            out[m].x = p->x + (mul16((int16_t)(q->x - p->x), f) >> 12);
            out[m].y = p->y + (mul16((int16_t)(q->y - p->y), f) >> 12);
            out[m].z = NEAR_Z;
            m++;
        }
    }
    if (m < 3) return;
    xy = add_poly_slot(depth, col, m);
    if (!xy) return;
    for (i = 0; i < m; i++, xy += 2) project((int16_t)out[i].x, (int16_t)out[i].y, (int16_t)out[i].z, xy);
}

/* ------------------------------------------------------------ terrain */

#define GR 5                    /* tiles each side of the grid centre (port: the original's 7) */
#define GN (2 * GR + 2)         /* vertices per side */
#define GS 16                   /* the arrays' row stride (tgrid.asm) */
int16_t gx[GS * GS], gy[GS * GS], gz[GS * GS];          /* camera space */

/* tgrid.asm: a row of grid vertices */
extern int32_t tg_X, tg_Y, tg_Z, tg_AX, tg_AY, tg_AZ;
extern int16_t tg_cp, tg_sp;
extern const int16_t *tg_hrow;
extern uint8_t tg_col, tg_n;
extern uint16_t tg_k;
extern void tg_row(void);
/* tgrid.asm: a row of tiles */
extern uint8_t tq_n, tq_j, tq_tx, tq_tz, tq_padx, tq_padz, tq_nclip;
extern uint8_t tq_clip[];
extern void tq_row(void);
static int16_t gx0, gz0;
uint16_t clip_n, clip_kept;


static void draw_terrain(const World *w)
{
    int32_t fx = cam.x + ((TS_SIN(cam.yaw) * ((GR - 2) * TILE)) >> 16);
    int32_t fz = cam.z + ((TS_COS(cam.yaw) * ((GR - 2) * TILE)) >> 16);
    int32_t rx0, rz0;
    int16_t dx0, dz0, cyh;
    uint8_t i, j;

    gx0 = (int16_t)(ts_wrap(fx) >> TILE_BITS) - GR;
    gz0 = (int16_t)(ts_wrap(fz) >> TILE_BITS) - GR;

    /* port: camera space by sums (tgrid.asm does a row: its x, y, z grow by
       constants from vertex to vertex, plus the height's products). With
       dx, dz the first vertex's offsets from the camera:
         x = (dx cy - dz sy) >> 12                     one tile across: + cy << 8
         y = (rz sp + (h - camy) cp) >> 12             (rz the rotated z)
         z = (rz cp - (h - camy) sp) >> 12 */
    dx0 = (int16_t)ts_wrap_delta((int32_t)gx0 << TILE_BITS, cam.x);
    dz0 = (int16_t)ts_wrap_delta((int32_t)gz0 << TILE_BITS, cam.z);
    rx0 = mul16(dx0, ccy) - mul16(dz0, csy);
    rz0 = mul16(dx0, csy) + mul16(dz0, ccy);
    cyh = (int16_t)cam.y;
    {
        int16_t rz = (int16_t)(rz0 >> 12);
        int32_t x = rx0, y = mul16(rz, csp) - mul16(cyh, ccp), z = mul16(rz, ccp) + mul16(cyh, csp);
        int32_t bx = -((int32_t)csy << TILE_BITS);             /* a row down (z + 1 tile) */
        int32_t by = mul16(ccy, csp) >> (12 - TILE_BITS);
        int32_t bz = mul16(ccy, ccp) >> (12 - TILE_BITS);
        tg_AX = (int32_t)ccy << TILE_BITS;                     /* a vertex across (x + 1 tile) */
        tg_AY = mul16(csy, csp) >> (12 - TILE_BITS);
        tg_AZ = mul16(csy, ccp) >> (12 - TILE_BITS);
        tg_cp = ccp;
        tg_sp = csp;
        tg_n = GN;
        for (j = 0; j < GN; j++, x += bx, y += by, z += bz) {
            tg_k = j * GS;
            tg_X = x;
            tg_Y = y;
            tg_Z = z;
            tg_col = (uint8_t)gx0;
            tg_hrow = TS_HEIGHTS + (((uint16_t)(gz0 + j) & (MAP_N - 1)) << MAP_BITS);
            tg_row();
        }
    }

    prof(5);
    /* the tiles, a row at a time (tgrid.asm tq_row: culls, the original's
       pens, depth buckets); tiles crossing the near plane come back here */
    tq_n = GN - 1;
    tq_tx = (uint8_t)gx0 & (MAP_N - 1);
    tq_padx = (uint8_t)w->pad_tx;
    tq_padz = (uint8_t)w->pad_tz;
    for (j = 0; j < GN - 1; j++) {
        uint8_t c;
        tq_j = j;
        tq_tz = (uint8_t)(gz0 + j) & (MAP_N - 1);
        tq_nclip = 0;
        tq_row();
        clip_n += tq_nclip >> 1;
        for (c = 0; c < tq_nclip; c += 2) {
            uint16_t ia, ib, ic, id, k[4];
            uint8_t v, below = 1;
            V3 q[4];
            i = tq_clip[c];
            ia = j * GS + i; ib = ia + 1; ic = ia + GS + 1; id = ia + GS;
            /* port: a tile across the near plane whose corners in front all
               project under the view, the rest under the eye, is under the
               view: skip it (the original clips it, then drops it) */
            k[0] = ia; k[1] = ib; k[2] = ic; k[3] = id;
            for (v = 0; v < 4 && below; v++)
                below = gz[k[v]] >= NEAR_Z ? gs_y[k[v]] >= R_ROWS : gy[k[v]] < 0;
            if (below) continue;
            clip_kept++;
            q[0].x = gx[ia]; q[0].y = gy[ia]; q[0].z = gz[ia];
            q[1].x = gx[ib]; q[1].y = gy[ib]; q[1].z = gz[ib];
            q[2].x = gx[ic]; q[2].y = gy[ic]; q[2].z = gz[ic];
            q[3].x = gx[id]; q[3].y = gy[id]; q[3].z = gz[id];
            add_clipped(tdepth[j * (GS - 1) + i], tq_clip[c + 1], q, 4);
        }
    }
}

/* the depth key of something standing over world (x, z): its tile's depth,
   nudged nearer, or its own depth outside the grid */
static int16_t key_for(int32_t x, int32_t z, int16_t own)
{
    uint8_t i = ((uint8_t)(ts_wrap(x) >> TILE_BITS) - (uint8_t)gx0) & (MAP_N - 1);
    uint8_t j = ((uint8_t)(ts_wrap(z) >> TILE_BITS) - (uint8_t)gz0) & (MAP_N - 1);
    if (i < GN - 1 && j < GN - 1) return tdepth[j * (GS - 1) + i] - 6;
    return own;
}

/* ------------------------------------------------------------ models */

static void add_model(uint8_t mdl, int32_t wx, int32_t wy, int32_t wz,
                      uint8_t yaw, int8_t tilt, int8_t bank, uint8_t flat_y, int16_t key_adj)
{
    static int16_t tilted[MDL_MAXV][3];
    const MModel *m = &mdl_models[mdl];
    const MFace *f = mdl_faces + m->f0;
    V3 ctr;
    int16_t co = (int16_t)(TS_COS(yaw) >> 4), si = (int16_t)(TS_SIN(yaw) >> 4), key;
    uint8_t i, k;

    prof(6);
    xform(wx, wy, wz, &ctr);
    if (ctr.z < -200 || ctr.z > FAR_Z) return;
    if (ctr.x > ctr.z + 250 || ctr.x < -ctr.z - 250) return;
    key = key_for(wx, wz, (int16_t)ctr.z) - key_adj;

    /* port: the model's yaw and the camera's as one matrix, the vertices in
       asm (xf_model); the original rotates each vertex, then cam_xforms it */
    xf_m00 = (int16_t)((mul16(co, ccy) + mul16(si, csy)) >> 12);
    xf_m01 = (int16_t)((mul16(si, ccy) - mul16(co, csy)) >> 12);
    xf_m10 = (int16_t)((mul16(co, csy) - mul16(si, ccy)) >> 12);
    xf_m11 = (int16_t)((mul16(si, csy) + mul16(co, ccy)) >> 12);
    xf_cx = (int16_t)ctr.x;
    xf_cy = (int16_t)ctr.y;
    xf_cz = (int16_t)ctr.z;
    xf_flat = flat_y;
    xf_n = m->nv;
    xf_src = mdl_verts[m->v0];
    if (tilt || bank) {
        /* the craft: nose up when climbing, a wing dipped in turns */
        for (i = 0; i < m->nv; i++) {
            const int16_t *v = mdl_verts[m->v0 + i];
            tilted[i][0] = v[0];
            tilted[i][1] = v[1] + ((v[2] * tilt) >> 6) + ((v[0] * bank) >> 3);
            tilted[i][2] = v[2];
        }
        xf_src = tilted[0];
    }
    prof(8);
    xf_model();
    prof(9);

    for (k = 0; k < m->nf; k++, f++) {
        uint8_t a = f->idx[0], b = f->idx[1], c = f->idx[2], n = f->n;
        if (mvz[a] >= NEAR_Z && mvz[b] >= NEAR_Z && mvz[c] >= NEAR_Z &&
            (n == 3 || mvz[f->idx[3]] >= NEAR_Z)) {
            /* port: the back face test by the winding on screen */
            int32_t cross = mul16(msx[b] - msx[a], msy[c] - msy[a]) -
                            mul16(msy[b] - msy[a], msx[c] - msx[a]);
            int16_t *xy;
            uint8_t v;
            if (cross <= 0) continue;
            xy = add_poly_slot(key, f->col, n);
            if (!xy) return;
            for (v = 0; v < n; v++, xy += 2) {
                xy[0] = msx[f->idx[v]];
                xy[1] = msy[f->idx[v]];
            }
        } else {
            /* near the camera: the original's camera-space test, then clip */
            V3 q[4];
            int32_t ux, uy, uz, vx, vy, vz;
            uint8_t v;
            for (v = 0; v < n; v++) {
                q[v].x = mvx[f->idx[v]];
                q[v].y = mvy[f->idx[v]];
                q[v].z = mvz[f->idx[v]];
            }
            ux = q[1].x - q[0].x; uy = q[1].y - q[0].y; uz = q[1].z - q[0].z;
            vx = q[2].x - q[0].x; vy = q[2].y - q[0].y; vz = q[2].z - q[0].z;
            if ((uy * vz - uz * vy) * q[0].x + (uz * vx - ux * vz) * q[0].y +
                (ux * vy - uy * vx) * q[0].z >= 0) continue;
            add_clipped(key, f->col, q, n);
        }
    }
    render_stats.models++;
    prof(4);
}

static uint8_t ent_model(const Ent *e, uint8_t idx)
{
    switch (e->type) {
    case ENT_TANK: return MDL_TANK;
    case ENT_BUILDING: return (idx & 1) ? MDL_TOWER : MDL_BUILDING;
    case ENT_SHIP: return MDL_SHIP;
    case ENT_TREE: return MDL_TREE;
    }
    return 255;
}

/* ------------------------------------------------------------ points (sprites) */

static void add_point(int32_t x, int32_t y, int32_t z, uint8_t col, uint8_t big)
{
    V3 v;
    RPoint *p;
    int16_t sx, sy, size;
    if (render_npoints >= POINT_SPRITES) return;   /* port: 80 at once (sprites) */
    xform(x, y, z, &v);
    if (v.z < NEAR_Z || v.z > FAR_Z) return;
    sx = msx[0];
    sy = msy[0];
    if (sx < 0 || sx >= R_W || sy < 0 || sy >= R_ROWS) return;
    p = &render_points[render_npoints++];
    p->x = (uint8_t)sx;
    p->y = (uint8_t)sy;
    p->col = col;
    /* the original's big * 300 / z, by the projection's 1/z (port) */
    size = (int16_t)(mul16(big * 300 / R_FOCAL, (int16_t)recip((int16_t)v.z)) >> 12);
    p->size = size < 1 ? 1 : size > 5 ? 5 : (uint8_t)size;
}

/* ------------------------------------------------------------ the frame */

void render_init(void)
{
    uint16_t i;
    for (i = 0; i < 256; i++) {
        uint16_t z = i ? i * 4 : 2;
        uint32_t r = ((uint32_t)R_FOCAL << 12) / z;
        rnear[i] = r > 32767 ? 32767 : (uint16_t)r;
    }
    for (i = 0; i < sizeof(rfar) / 2; i++) {
        uint16_t z = i * 32 + 16;
        rfar[i] = (uint16_t)(((uint32_t)R_FOCAL << 12) / z);
    }
}

void render_build(const World *w)
{
    uint8_t i;
    arena_used = POLY_FIRST;
    memset(bhead, 0, 4 * NBUCKET);
    render_stats.tiles = render_stats.models = render_stats.polys = render_stats.dropped = 0;
    render_npoints = 0;
    ccy = (int16_t)(cam.cy >> 4);
    csy = (int16_t)(cam.sy >> 4);
    ccp = (int16_t)(cam.cp >> 4);
    csp = (int16_t)(cam.sp >> 4);

    draw_terrain(w);
    prof(6);
    for (i = 0; i < MAX_ENTS; i++) {
        const Ent *e = &w->ents[i];
        uint8_t m = ent_model(e, i);
        int16_t dx, dz;
        if (m == 255) continue;
        /* port: beyond FAR_Z (+ the original's 250 margin) whichever way the
           camera looks: not transformed at all */
        dx = wrap16(e->x >> FBITS, cam.x);
        dz = wrap16(e->z >> FBITS, cam.z);
        if (dx > FAR_Z + 250 || dx < -FAR_Z - 250 || dz > FAR_Z + 250 || dz < -FAR_Z - 250) continue;
        add_model(m, e->x >> FBITS, e->y >> FBITS, e->z >> FBITS, (uint8_t)e->yaw, 0, 0, 0, 0);
    }
    if (w->state != GS_TITLE && w->p.state != PS_DEAD) {
        const Craft *p = &w->p;
        int32_t px = p->x >> FBITS, py = p->y >> FBITS, pz = p->z >> FBITS;
        int16_t tilt = (int16_t)(p->vy / 8);
        if (tilt > 6) tilt = 6;
        if (tilt < -6) tilt = -6;
        add_model(MDL_SHADOW, px, ground_height(w, px, pz) + 2, pz, (uint8_t)p->yaw, 0, 0, 1, 0);
        add_model(MDL_CRAFT, px, py, pz, (uint8_t)p->yaw, (int8_t)tilt, (int8_t)p->bank, 0, 4);
    }
    prof(7);
    for (i = 0; i < MAX_SHOTS; i++)
        if (w->shots[i].active)
            add_point(w->shots[i].x >> FBITS, w->shots[i].y >> FBITS, w->shots[i].z >> FBITS, PEN_YELLOW, 8);
    for (i = 0; i < MAX_SHELLS; i++)
        if (w->shells[i].active)
            add_point(w->shells[i].x >> FBITS, w->shells[i].y >> FBITS, w->shells[i].z >> FBITS, PEN_RED, 10);
    for (i = 0; i < MAX_PARTS; i++)
        if (w->parts[i].life > 0)
            add_point(w->parts[i].x >> FBITS, w->parts[i].y >> FBITS, w->parts[i].z >> FBITS,
                      (uint8_t)w->parts[i].col, 10);
}

/* the buckets far to near into the back buffer (the heights' page in slot 3
   is swapped out while l2poly draws) */
void render_draw(void)
{
    int16_t b;
    l2p_top = 0;
    l2p_bot = R_ROWS;
    l2p_xmin = 0;
    l2p_xmax = 255;
    l2p_begin();
    l2p_rect(0, R_ROWS, 0, 255, PEN_BLACK);
    for (b = NBUCKET - 1; b >= 0; b--) {
        uint16_t o = bhead[b];
        while (o) {
            const uint8_t *p = arena + o - 1;
            l2p_poly((const int16_t *)(p + 4), p[2], p[3]);
            o = *(const uint16_t *)p;
        }
    }
    l2p_end();
}
