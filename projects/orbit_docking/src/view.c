/*
 * view.c - Orbit Docking's picture from the cockpit (the original's
 * render.c scene()): the stars, the spinning station (its hub, the slot
 * and its lights, four arms, the ring, two solar panels), the approach
 * markers and the debris, on Layer 2.
 *
 * The camera always looks straight back down the station's axis (yaw half
 * a turn, no pitch) and rolls with the craft. So every point of a plane
 * across the axis (the hub's face, the ring, ...) is at the same depth,
 * and its picture is the plane's shape turned and scaled: for a plane at
 * depth Z, a point (x, y) of the station lands at
 *
 *   sx = 128 + k (y sin d - x cos d) + k Tx,  sy = 96 - k (x sin d + y cos d) - k Ty
 *
 * with d the station's turn less the craft's roll, k = 184 / Z, and (Tx, Ty)
 * the camera's offset from the axis turned by the roll. The markers and the
 * debris stand still: the same with d = -roll. So a frame works out two
 * coefficients a plane and four multiplies a corner (the original: a full
 * camera transform a corner).
 *
 * port: the original's 320 x 256 view (fov 230, centre 160, 128) at 0.8 on
 * Layer 2's 256 x 192 (fov 184, centre 128, 96); 6 rows top and bottom are
 * off the screen. It is drawn 96 rows high and shown 192 (main.c's copper
 * shows each row twice): half the rows to fill. The original's painter's order (a3d's 512 depth buckets
 * of a polygon's mean depth plus its bias) is kept; the hub's and the
 * ring's sides that face away (hidden behind the rest in the original) are
 * not drawn. A polygon with a corner nearer than the near plane is left
 * out (the camera only gets there beside the hub, about to crash).
 */
#include <string.h>
#include "nextgfx.h"
#include "l2poly.h"
#include "fixmath.h"
#include "view.h"

uint16_t view_polys;
int16_t view_debris_x[OD_MAX_DEBRIS];

#define FOV   184
#define CX    128
#define CY    48                         /* port: drawn 96 rows high, each shown twice (main.c) */
#define ROWS  96
#define NEAR  12

/* the planes across the axis: z (world), and whether they turn with the station */
enum { P_FACE, P_SLOT, P_ARM, P_ARM_B, P_PANEL, P_RING_B, P_HUB_B, P_M0, P_M1, P_M2, NPLANES };
static const int16_t plane_z[NPLANES] = { OD_PORT_Z, OD_PORT_Z + 1, -60, -100, -90, -120, -300, 420, 260, 150 };
#define P_STATIC P_M0                     /* the markers stand still */

#define MAXV 200                          /* 160 used */
#define MAXP 72                           /* 67 used */
static int16_t vx[MAXV] = { 0 }, vy[MAXV] = { 0 };
static uint8_t vplane[MAXV] = { 0 };
static uint8_t nv;
static int16_t sx[MAXV] = { 0 }, sy[MAXV] = { 0 };
static uint8_t plane_end[NPLANES] = { 0 };
static int16_t plane_max[NPLANES] = { 0 };   /* each plane's largest |x| or |y| */
static uint8_t vrec[MAXV * 5] = { 0 };       /* the corners as vproj's records */

/* vproj.asm: one plane's corners */
extern int16_t pj_e, pj_f, pj_c, pj_d;
extern const uint8_t *pj_rec;             /* corner records: |x|, |y| (2 bytes each), signs (bit 0 x, bit 1 y) */
extern int16_t *pj_sx, *pj_sy;
extern uint8_t pj_n;
extern void vproj(void);        /* the corners are sorted by plane: plane p's end at plane_end[p] */

typedef struct {
    uint8_t n, col, cull;                /* cull: 0 none, 1 a hub side, 2 a ring side (the mid angle in ang) */
    uint8_t pa, pb;                      /* its corners' planes (two each, or all on one) */
    int8_t bias;
    uint16_t ang;
    int16_t ca, sa;                      /* cos, sin of ang (Q14), for faces() */
    uint8_t v[8];
} Poly;
static Poly poly[MAXP] = { { 0 } };
static uint8_t npoly;
static uint8_t marker_first[3] = { 0 };          /* each marker's first polygon */

static int16_t zc[NPLANES] = { 0 };

#define NB 128                            /* port: 128 buckets (4 of the original's each) */
static uint8_t bhead[NB] = { 0 }, bnext[MAXP + OD_MAX_DEBRIS] = { 0 };
static uint8_t btail[NB] = { 0 };
              /* each plane's depth from the camera */

/* the stars (render.c stars()): offsets from the middle, the original's hash */
static int16_t star_x[60] = { 0 }, star_y[60] = { 0 };
static uint8_t star_rec[60 * 5] = { 0 };


static uint32_t a3_hash(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dUL;
    x ^= x >> 15;
    x *= 0x846ca68bUL;
    x ^= x >> 16;
    return x;
}

static uint8_t vert(int32_t x, int32_t y, uint8_t plane)
{
    uint8_t i;
    for (i = 0; i < nv; i++)                 /* corners shared by polygons once */
        if (vx[i] == (int16_t)x && vy[i] == (int16_t)y && vplane[i] == plane) return i;
    vx[nv] = (int16_t)x;
    vy[nv] = (int16_t)y;
    vplane[nv] = plane;
    return nv++;
}

static void make_rec(uint8_t *r, int16_t x, int16_t y)
{
    uint16_t ax = x < 0 ? -x : x, ay = y < 0 ? -y : y;
    r[0] = (uint8_t)ax;
    r[1] = (uint8_t)(ax >> 8);
    r[2] = (uint8_t)ay;
    r[3] = (uint8_t)(ay >> 8);
    r[4] = (x < 0 ? 1 : 0) | (y < 0 ? 2 : 0);
}

static Poly *add(uint8_t col, int8_t bias)
{
    Poly *p = &poly[npoly++];
    memset(p, 0, sizeof(*p));
    p->col = col;
    p->bias = bias;
    return p;
}

/* the original's squad(): a quad, two corners on each of two planes */
static Poly *squad(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3,
                   uint8_t p0, uint8_t p1, int8_t bias, uint8_t col)
{
    Poly *p = add(col, bias);
    p->n = 4;
    p->v[0] = vert(x0, y0, p0);
    p->v[1] = vert(x1, y1, p0);
    p->v[2] = vert(x2, y2, p1);
    p->v[3] = vert(x3, y3, p1);
    return p;
}

#define RMUL(c, r) ((int32_t)(((int32_t)(c) * (r)) >> 16))

void view_init(void)
{
    uint8_t k;
    Poly *p;
    nv = npoly = 0;
    /* the hub: eight sides and its face */
    for (k = 0; k < 8; k++) {
        uint16_t a0 = k * 8192u + 4096u, a1 = a0 + 8192u;
        int32_t x0 = RMUL(a3_cos(a0), OD_HUB_R), y0 = RMUL(a3_sin(a0), OD_HUB_R);
        int32_t x1 = RMUL(a3_cos(a1), OD_HUB_R), y1 = RMUL(a3_sin(a1), OD_HUB_R);
        p = squad(x0, y0, x1, y1, x1, y1, x0, y0, P_FACE, P_HUB_B, 0, PEN_HUB + (k & 1));
        p->cull = 1;
        p->ang = a0 + 4096u;
    }
    p = add(PEN_FACE, -4);
    p->n = 8;
    for (k = 0; k < 8; k++) p->v[k] = poly[k].v[0];
    /* the slot and its lights */
    squad(-24, -7, 24, -7, 24, 7, -24, 7, P_SLOT, P_SLOT, -8, PEN_SLOT);
    squad(-34, -4, -28, -4, -28, 4, -34, 4, P_SLOT, P_SLOT, -9, PEN_LIGHT);
    squad(28, -4, 34, -4, 34, 4, 28, 4, P_SLOT, P_SLOT, -9, PEN_LIGHT);
    squad(-3, 30, 3, 30, 3, 36, -3, 36, P_SLOT, P_SLOT, -9, PEN_RLIGHT);
    /* four arms, out to the ring */
    for (k = 0; k < 4; k++) {
        int32_t c = a3_cos(k * 16384u), s = a3_sin(k * 16384u);
        int32_t ax = RMUL(c, 60), ay = RMUL(s, 60), bx = RMUL(c, 260), by = RMUL(s, 260);
        int32_t px = RMUL(-s, 10), py = RMUL(c, 10);
        squad(ax + px, ay + py, bx + px, by + py, bx - px, by - py, ax - px, ay - py, P_ARM, P_ARM, 0, PEN_ARM);
        squad(ax + px, ay + py, bx + px, by + py, bx + px, by + py, ax + px, ay + py, P_ARM, P_ARM_B, 2, PEN_ARM + 1);
    }
    /* the ring */
    for (k = 0; k < 16; k++) {
        uint16_t a0 = k * 4096u, a1 = a0 + 4096u;
        int32_t c0 = a3_cos(a0), s0 = a3_sin(a0), c1 = a3_cos(a1), s1 = a3_sin(a1);
        squad(RMUL(c0, 260), RMUL(s0, 260), RMUL(c1, 260), RMUL(s1, 260), RMUL(c1, 300), RMUL(s1, 300), RMUL(c0, 300),
              RMUL(s0, 300), P_ARM, P_ARM, 0, PEN_RING + (k & 1));
        p = squad(RMUL(c0, 300), RMUL(s0, 300), RMUL(c1, 300), RMUL(s1, 300), RMUL(c1, 300), RMUL(s1, 300),
                  RMUL(c0, 300), RMUL(s0, 300), P_ARM, P_RING_B, 2, PEN_RING + 1);
        p->cull = 2;
        p->ang = a0 + 2048u;
    }
    /* solar panels beyond the ring */
    for (k = 0; k < 2; k++) {
        int16_t sg = k ? -1 : 1;
        squad(sg * 310, -40, sg * 470, -40, sg * 470, 40, sg * 310, 40, P_PANEL, P_PANEL, 0, PEN_PANEL + k);
    }
    /* the approach markers: squares round the axis, four bars each */
    for (k = 0; k < 3; k++) {
        uint8_t s;
        int16_t h = 34, t = 2;
        marker_first[k] = npoly;
        for (s = 0; s < 4; s++) {
            int16_t x0 = s == 1 ? h - t : -h, x1 = s == 3 ? -h + t : h, y0 = s == 0 ? h - t : -h, y1 = s == 2 ? -h + t : h;
            squad(x0, y0, x1, y0, x1, y1, x0, y1, P_M0 + k, P_M0 + k, -2, PEN_MARK);
        }
    }
    /* the corners in plane order (each plane's projected in one run) */
    {
        uint8_t *map = (uint8_t *)bnext;         /* (scratch: sx, sy, bnext are free until a frame) */
        int16_t *tx_ = sx, *ty_ = sy;
        uint8_t n = 0, pl, i, j;
        for (pl = 0; pl < NPLANES; pl++) {
            for (i = 0; i < nv; i++)
                if (vplane[i] == pl) { map[i] = n; tx_[n] = vx[i]; ty_[n] = vy[i]; n++; }
            plane_end[pl] = n;
        }
        for (i = 0; i < nv; i++) { vx[i] = tx_[i]; vy[i] = ty_[i]; }
        for (i = 0; i < npoly; i++)
            for (j = 0; j < poly[i].n; j++) poly[i].v[j] = map[poly[i].v[j]];
        for (pl = 0, i = 0; i < nv; i++) {
            int16_t m;
            while (i >= plane_end[pl]) pl++;
            vplane[i] = pl;
            m = vx[i] < 0 ? -vx[i] : vx[i];
            if (m > plane_max[pl]) plane_max[pl] = m;
            m = vy[i] < 0 ? -vy[i] : vy[i];
            if (m > plane_max[pl]) plane_max[pl] = m;
            make_rec(&vrec[i * 5], vx[i], vy[i]);
        }
        /* each polygon's planes: its first and last corners' (a polygon's
           corners are on one plane or two, two each) */
        for (i = 0; i < npoly; i++) {
            poly[i].pa = vplane[poly[i].v[0]];
            poly[i].pb = vplane[poly[i].v[poly[i].n - 1]];
            poly[i].ca = (int16_t)(a3_cos(poly[i].ang) >> 2);
            poly[i].sa = (int16_t)(a3_sin(poly[i].ang) >> 2);
        }
    }
    for (k = 0; k < 60; k++) {
        uint32_t h = a3_hash((uint32_t)k * 2654435761UL);
        star_x[k] = (int16_t)(h % 640) - 320;
        star_y[k] = 320 - (int16_t)((h >> 10) % 640);   /* (y negated: see stars()) */
        make_rec(&star_rec[k * 5], star_x[k], star_y[k]);
    }
}

/* ---- the frame ---- */

/* the draw order: a3d's buckets (mean depth + bias, 512 over 0..2500),
   far first, in the order added within a bucket */

static void bucket_add(uint8_t i, int16_t depth)
{
    int16_t b = depth <= 0 ? 0 : depth >= 2500 ? NB - 1 : (int16_t)(((int32_t)depth * 13) >> 8);   /* ~ depth * 128 / 2500 */
    if (b >= NB) b = NB - 1;
    bnext[i] = 0xFF;
    if (bhead[b] == 0xFF) bhead[b] = i; else bnext[btail[b]] = i;
    btail[b] = i;
}

/* the debris: hexagons spinning in the view (render.c debris()) */
#define DEB MAXP                          /* their polygon numbers: DEB + k */
static int16_t deb_xy[OD_MAX_DEBRIS][12] = { { 0 } };

static int32_t rc, rs;                    /* the roll, 16.16 */
static int16_t tx, ty;                    /* the camera's offset turned by the roll */

static void project_planes(const ODGame *g, int16_t cz)
{
    uint8_t pl, i;
    uint16_t ra = (uint16_t)(g->roll >> 8), sa = (uint16_t)(g->st_angle >> 8);
    int16_t cdr, sdr, cds, sds;            /* Q14: the station's d, the still planes' d */
    {
        uint16_t d = sa - ra;
        cds = (int16_t)(a3_cos(d) >> 2);
        sds = (int16_t)(a3_sin(d) >> 2);
        cdr = (int16_t)(rc >> 2);
        sdr = (int16_t)(-rs >> 2);
    }
    for (pl = 0; pl < NPLANES; pl++) {
        int16_t z = cz - plane_z[pl], e, f, c, d;
        int32_t k;                         /* FOV / z, Q11 */
        zc[pl] = z;
        if (z < NEAR) continue;
        k = ((int32_t)FOV << 11) / z;
        if (pl < P_STATIC) { e = (int16_t)((k * sds) >> 14); f = (int16_t)((k * cds) >> 14); }
        else { e = (int16_t)((k * sdr) >> 14); f = (int16_t)((k * cdr) >> 14); }
        c = (int16_t)(((int32_t)tx * k) >> 11);
        d = (int16_t)(((int32_t)ty * k) >> 12);
        /* vproj keeps products in 24 bits: a plane too near for that is left out */
        if ((int32_t)((e < 0 ? -e : e) + (f < 0 ? -f : f)) * plane_max[pl] >= (1L << 22)) { zc[pl] = 0; continue; }
        i = pl ? plane_end[pl - 1] : 0;
        pj_e = e;
        pj_f = f;
        pj_c = CX + c;
        pj_d = CY - d;
        pj_rec = &vrec[i * 5];
        pj_sx = &sx[i];
        pj_sy = &sy[i];
        pj_n = plane_end[pl] - i;
        vproj();
    }
}

/* the side facing the camera? (the camera in the station's frame against the side's plane) */
static int16_t cam_sx, cam_sy;           /* the camera across the axis, in the station's frame */

static uint8_t faces(const Poly *p)
{
    int32_t dist = mul16(cam_sx, p->ca) + mul16(cam_sy, p->sa);
    /* the side's plane: R cos(half the side's angle) out; Q14 */
    return dist > (p->cull == 1 ? 55L * 16384 : 294L * 16384);
}

static int16_t star_sx[60] = { 0 }, star_sy[60] = { 0 };

static void stars(void)
{
    uint8_t k;
    /* the stars turn with the roll, at 0.8 (port): vproj with e = 0.8 sin,
       f = -0.8 cos (Q11) on (x, -y) */
    pj_e = (int16_t)((rs * 4 / 5) >> 5);
    pj_f = (int16_t)(-(rc * 4 / 5) >> 5);
    pj_c = CX;
    pj_d = CY;
    pj_rec = star_rec;
    pj_sx = star_sx;
    pj_sy = star_sy;
    pj_n = 60;
    vproj();
    l2p_begin();
    l2p_bot = ROWS;
    l2p_rect(0, ROWS, 0, 255, PEN_SPACE);       /* (not l2_clear: its DMA takes slots 0-1, this code's) */
    for (k = 0; k < 60; k++) {
        int16_t x = star_sx[k], y = star_sy[k];
        if (x >= 0 && y >= 0 && x < 256 && y < ROWS) l2p_span((uint8_t)y, x, x, (k & 3) ? PEN_STAR : 1);
    }
}

void view_draw(const ODGame *g)
{
    int16_t cz = (int16_t)(g->z >> 16), cx = (int16_t)(g->x >> 16), cy = (int16_t)(g->y >> 16);
    uint16_t ra = (uint16_t)(g->roll >> 8), sa = (uint16_t)(g->st_angle >> 8);
    uint8_t i, b, k;
    int16_t xy[16];
    rc = a3_cos(ra);
    rs = a3_sin(ra);
    tx = (int16_t)((cx * rc + cy * rs) >> 16);
    ty = (int16_t)((cx * rs - cy * rc) >> 16);
    {
        int32_t c = a3_cos(sa), s = a3_sin(sa);   /* the camera turned back by the station's turn */
        cam_sx = (int16_t)((cx * c + cy * s) >> 16);
        cam_sy = (int16_t)((cy * c - cx * s) >> 16);
    }
    project_planes(g, cz);
    memset(bhead, 0xFF, sizeof(bhead));
    view_polys = 0;
    for (i = 0; i < npoly; i++) {
        Poly *p = &poly[i];
        int16_t depth;
        if (i >= marker_first[0] && i < marker_first[0] + 12) {
            k = (i - marker_first[0]) >> 2;
            if ((int32_t)plane_z[P_M0 + k] << 16 >= g->z - FROMI(20)) continue;   /* behind us */
        }
        if (p->cull && !faces(p)) continue;
        {
            int16_t za = zc[p->pa], zb = zc[p->pb];
            if (za < NEAR || zb < NEAR) continue;
            if (p->n == 8) depth = za - 4;           /* the face: its first corner's depth - 4 */
            else depth = ((za + zb) >> 1) + p->bias;  /* (the four corners' mean: two on each plane) */
        }
        bucket_add(i, depth);
    }
    /* the debris */
    for (k = 0; k < g->ndebris; k++) {
        const Debris *d = &g->debris[k];
        int16_t dx = view_debris_x[k], z = cz - (int16_t)d->z, j;
        int16_t ex, ey, kk;
        uint16_t ang = (uint16_t)(g->level_ticks * 900 + k * 9000L);
        if (z < NEAR + 30) continue;
        /* the centre in the view: the still planes' transform */
        ex = (int16_t)(-((dx * rc + d->y * rs) >> 16) + tx);
        ey = (int16_t)(((-dx * rs + d->y * rc) >> 16) + ty);
        kk = (int16_t)(((int32_t)FOV << 8) / z);  /* Q8 */
        for (j = 0; j < 6; j++) {
            uint16_t a = ang + j * 10923u;
            int16_t r = (j & 1) ? 14 : 24;
            int16_t px = ex + (int16_t)RMUL(a3_cos(a), r), py = ey + (int16_t)RMUL(a3_sin(a), r);
            deb_xy[k][j * 2] = CX + (int16_t)(mul16(px, kk) >> 8);
            deb_xy[k][j * 2 + 1] = CY - (int16_t)(mul16(py, kk) >> 9);
        }
        bucket_add(DEB + k, z);
    }
    stars();
    for (b = NB; b-- > 0;) {
        uint8_t n = bhead[b];
        while (n != 0xFF) {
            if (n >= DEB) {
                l2p_poly(deb_xy[n - DEB], 6, PEN_DEBRIS);
            } else {
                const Poly *p = &poly[n];
                const uint8_t *v = p->v;
                int16_t *o = xy;
                uint8_t j;
                for (j = p->n; j; j--) {
                    uint8_t c = *v++;
                    *o++ = sx[c];
                    *o++ = sy[c];
                }
                l2p_poly(xy, p->n, p->col);
            }
            view_polys++;
            n = bnext[n];
        }
    }
    /* the sight (render.c hud()), at 0.8 */
    l2p_rect(CY, CY + 1, CX - 10, CX - 4, PEN_YELLOW);
    l2p_rect(CY, CY + 1, CX + 4, CX + 10, PEN_YELLOW);
    l2p_rect(CY - 5, CY - 2, CX, CX + 1, PEN_YELLOW);
    l2p_end();
}
