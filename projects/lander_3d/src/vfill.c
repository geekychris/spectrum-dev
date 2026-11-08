/*
 * vfill.c - the view's drawing (see vfill.h): the sky and the plain, the
 * stars, the ground strips near to far into the columns' highest rows
 * (vf_ybuf), then the polygons over it, each clipped by a snapshot of
 * vf_ybuf.
 *
 * A polygon is drawn with the ground's runs (vfill.asm): its lower edges
 * write each column's bottom into vf_tmp (clipped by its snapshot), then its
 * upper edges fill down to it. On the host the runs are C (the same steps).
 */
#include <string.h>
#include "vfill.h"
#include "fixmath.h"
#include "prof.h"
#ifdef __SDCC
#include <arch/zxn.h>
#include "nextgfx.h"
#endif

VfVtx vf_v[NLINE][NVX];
uint8_t vf_col[NLINE - 1][NVX - 1];
uint8_t vf_nlines;
uint8_t vf_snap_at[VF_SNAPS];
uint8_t vf_sky, vf_plain, vf_hz;
uint8_t vf_star_x[VF_NSTARS], vf_star_y[VF_NSTARS], vf_star_pen[VF_NSTARS];
uint8_t vf_nstars;
VfPoly vf_poly[VF_MAXPOLY];
VfVtx vf_pv[VF_MAXPT];
uint8_t vf_npoly;
uint16_t vf_npt;
uint8_t vf_bank0;
static uint8_t full_made;

void vf_clear_polys(void) { vf_npoly = 0; vf_npt = 0; }

uint8_t vf_add_poly(const VfVtx *v, uint8_t n, uint8_t pen, uint8_t snap_k)
{
    VfPoly *p;
    if (vf_npoly >= VF_MAXPOLY || vf_npt + n > VF_MAXPT || n < 3) return 0;
    p = &vf_poly[vf_npoly++];
    p->n = n;
    p->pen = pen;
    p->snap = snap_k;
    p->first = vf_npt;
    memcpy(&vf_pv[vf_npt], v, n * sizeof(VfVtx));
    vf_npt += n;
    return 1;
}

/* a polygon of the corners sv[idx[k] ^ x] (a box's face or outline) */
uint8_t vf_add_idx(const VfVtx *sv, const uint8_t *idx, uint8_t n, uint8_t x, uint8_t pen, uint8_t snap_k)
{
    VfPoly *p;
    VfVtx *d;
    if (vf_npoly >= VF_MAXPOLY || vf_npt + n > VF_MAXPT) return 0;
    p = &vf_poly[vf_npoly++];
    p->n = n;
    p->pen = pen;
    p->snap = snap_k;
    p->first = vf_npt;
    d = &vf_pv[vf_npt];
    vf_npt += n;
    while (n--) {
        const VfVtx *q = &sv[*idx++ ^ x];
        d->x = q->x;
        d->y = q->y;
        d++;
    }
    return 1;
}

#ifndef __SDCC
/* ---------------------------------------------------------------- the host:
   vfill.asm's steps in C */
uint8_t vf_ybuf[VW], vf_snap[VF_SNAPS][VW], vf_tmp[VW], vf_full[VW];
uint8_t vf_host_fb[VROWS * VW];
static uint16_t recip[768];
static uint8_t *run_top = vf_ybuf, *wrun_snap = vf_snap[0];

static void vspan(uint8_t x, uint8_t t, uint8_t b, uint8_t pen)
{
    uint8_t *p = VF_FB + (uint16_t)t * VW + x;
    while (t < b) { *p = pen; p += VW; t++; }
}

/* a line from (xa, ya) to (xb, yb), xa < xb: mode 0 fills each column from
   it down to run_top[x] (and sets run_top[x]); mode 1 writes vf_tmp[x] =
   min(its row + 1, wrun_snap[x]) */
static void run(int16_t xa, uint16_t ya, int16_t xb, uint16_t yb, uint8_t pen, uint8_t mode)
{
    int16_t x0, x1, dx = xb - xa, x;
    uint16_t k, ady, acc, slope;
    uint8_t neg;
    if (!recip[2]) { int16_t d; recip[1] = 65535; for (d = 2; d < 768; d++) recip[d] = (uint16_t)((65536L + d / 2) / d); }
    if (xb <= 0 || xa >= VW) return;
    x1 = xb > VW ? VW : xb;
    x0 = xa < 0 ? 0 : xa;
    k = xa < 0 ? (uint16_t)-xa : 0;
    neg = yb < ya;
    ady = neg ? ya - yb : yb - ya;
    slope = dx == 1 ? ady : (uint16_t)(((uint32_t)ady * recip[dx]) >> 16);
    if (slope & 0x8000) slope = 32767;
    if (neg) slope = (uint16_t)-slope;
    acc = (uint16_t)(ya + (uint16_t)(slope * k));
    for (x = x0; x < x1; x++, acc += slope) {
        uint8_t T = acc >> 8, b;
        if (mode) { vf_tmp[x] = T >= wrun_snap[x] ? wrun_snap[x] : T + 1; continue; }
        b = run_top[x];
        if (T >= b) continue;
        if (T < VF_OFF) T = VF_OFF;
        vspan((uint8_t)x, T - VF_OFF, b - VF_OFF, pen);
        run_top[x] = T;
    }
}

void vf_seg(const VfVtx *a, const VfVtx *b, uint8_t pen, uint8_t mode) { run(a->x, a->y, b->x, b->y, pen, mode); }
static void vf_poly_page(uint8_t *snap) { wrun_snap = snap; run_top = vf_tmp; }

void vf_ground(void)
{
    uint8_t s, q;
    int8_t k;
    run_top = vf_ybuf;
    memset(vf_ybuf, VROWS + VF_OFF, VW);
    for (s = 0; s + 1 < vf_nlines; s++) {
        const VfVtx *n = vf_v[s], *f = vf_v[s + 1];
        const uint8_t *col = vf_col[s];
        for (q = 0; q < VF_SNAPS; q++) if (vf_snap_at[q] == s) memcpy(vf_snap[q], vf_ybuf, VW);
        for (k = 1; k < NVX; k++)               /* edges leaning right: their left cell */
            if (n[k].x != VF_BEHIND && f[k].x != VF_BEHIND && n[k].x > f[k].x && col[k - 1] != VF_NONE)
                run(f[k].x, f[k].y, n[k].x, n[k].y, col[k - 1], 0);
        for (k = NVX - 2; k >= 0; k--)          /* leaning left: their right cell */
            if (n[k].x != VF_BEHIND && f[k].x != VF_BEHIND && n[k].x < f[k].x && col[k] != VF_NONE)
                run(n[k].x, n[k].y, f[k].x, f[k].y, col[k], 0);
        for (k = 0; k < NVX - 1; k++)
            if (col[k] != VF_NONE && f[k].x != VF_BEHIND && f[k + 1].x != VF_BEHIND && f[k].x < f[k + 1].x)
                run(f[k].x, f[k].y, f[k + 1].x, f[k + 1].y, col[k], 0);
    }
    for (q = 0; q < VF_SNAPS; q++) if (vf_snap_at[q] >= vf_nlines - 1) memcpy(vf_snap[q], vf_ybuf, VW);
}
#define SNAP_PAGE(q) ((q) < VF_SNAPS ? vf_snap[q] : vf_full)
#else
#define SNAP_PAGE(q) ((q) < VF_SNAPS ? (uint8_t)((uint16_t)vf_snap / 256 + (q)) : (uint8_t)((uint16_t)vf_full / 256))
#endif

/* a convex polygon: its chains from the leftmost point to the rightmost,
   the lower one written into vf_tmp, then the upper one filled down to it */
static void poly(const VfPoly *p)
{
    const VfVtx *v = &vf_pv[p->first];
    uint8_t n = p->n, k, l = 0, r = 0, up_fwd, i;
    int16_t ax, ay, bx, by;
    for (k = 1; k < n; k++) {
        if (v[k].x < v[l].x) l = k;
        if (v[k].x > v[r].x) r = k;
    }
    if (v[r].x <= 0 || v[l].x >= VW || v[l].x == v[r].x) return;
    /* the upper chain leaves l towards its neighbour with the smaller slope */
    {
        const VfVtx *L = &v[l], *N = &v[l + 1 == n ? 0 : l + 1], *P = &v[l ? l - 1 : n - 1];
        ax = N->x - L->x; ay = (int16_t)((N->y >> 8) - (L->y >> 8));
        bx = P->x - L->x; by = (int16_t)((P->y >> 8) - (L->y >> 8));
        up_fwd = mul16(ax, by) > mul16(ay, bx);
    }
    vf_poly_page(SNAP_PAGE(p->snap));
    /* its columns' bottoms start at the top (nothing drawn where its lower
       chain doesn't reach: a fill must never run past the view) */
    {
        int16_t x0 = v[l].x < 0 ? 0 : v[l].x, x1 = v[r].x > VW ? VW : v[r].x;
        if (x1 > x0) memset(&vf_tmp[x0], VF_OFF, (uint16_t)(x1 - x0));
    }
    /* the lower chain (mode 1), then the upper (mode 0) */
    for (k = 0; k < 2; k++) {
        uint8_t fwd = k ? up_fwd : !up_fwd, mode = k ? 0 : 1;
        i = l;
        while (i != r) {
            uint8_t j = fwd ? (i + 1 == n ? 0 : i + 1) : (i ? i - 1 : n - 1);
            if (v[i].x < v[j].x) vf_seg(&v[i], &v[j], p->pen, mode);
            i = j;
        }
    }
}

void vf_draw(void)
{
    uint8_t k, hz = vf_hz > VROWS ? VROWS : vf_hz;
    if (!full_made) { memset(vf_full, VROWS + VF_OFF, VW); full_made = 1; }
#ifdef __SDCC
    {
        uint8_t pg = l2_back * 2;
        ZXN_WRITE_MMU0(pg);
        ZXN_WRITE_MMU1(pg + 1);
        ZXN_WRITE_MMU2(pg + 2);
    }
    if (hz) dma_fill(VF_FB, vf_sky, (uint16_t)hz * VW);
    if (hz < VROWS) dma_fill(VF_FB + (uint16_t)hz * VW, vf_plain, (uint16_t)(VROWS - hz) * VW);
#else
    memset(VF_FB, vf_sky, (uint16_t)hz * VW);
    memset(VF_FB + (uint16_t)hz * VW, vf_plain, (uint16_t)(VROWS - hz) * VW);
#endif
    for (k = 0; k < vf_nstars; k++)
        if (vf_star_y[k] < hz) VF_FB[(uint16_t)vf_star_y[k] * VW + vf_star_x[k]] = vf_star_pen[k];
    PROF(PR_GROUND);
    vf_ground();
    PROF(PR_POLYS);
    for (k = 0; k < vf_npoly; k++) poly(&vf_poly[k]);
#ifdef __SDCC
    ZXN_WRITE_MMU0(vf_bank0 * 2);
    ZXN_WRITE_MMU1(vf_bank0 * 2 + 1);
    ZXN_WRITE_MMU2(10);
#endif
}
