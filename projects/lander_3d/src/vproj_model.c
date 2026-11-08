/* vproj_model.c - vproj.asm's steps in C, for the host build (the same
   arithmetic: the 12-bit shifts clamped to 16 bits, the reciprocal table,
   the clamps), so the host's pictures are the Next's */
#ifndef __SDCC
#include "ld.h"
#include "vfill.h"
#include "vproj.h"

int16_t vp_dx0, vp_dz0, vp_cell, vp_camy, vp_cp, vp_sp, vp_i0, vp_j0, vp_hb12;
uint8_t vp_n, vp_page, vp_rpp, vp_pen0, vp_r0;
int16_t vp_xlim[NLINE];
uint16_t vp_rowb, vp_hbf, vp_cdiv;
const uint8_t *host_page(uint8_t p);     /* host_ter.c */

static int16_t sh12(int32_t v)
{
    v >>= 12;
    return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v;
}

static uint16_t recip_s(int16_t z)
{
    static uint16_t st[256];
    int8_t b = 6;
    uint16_t zn = (uint16_t)z, s;
    if (!st[1]) { int i; for (i = 0; i < 256; i++) st[i] = (uint16_t)((655360.0 * 64 / (2048 + 8 * i + 4)) + 0.5); }
    while (zn >= 0x1000) { zn >>= 1; b++; }
    while (zn < 0x800) { zn <<= 1; b--; }
    s = st[(zn >> 3) & 255];
    return b < 0 ? (uint16_t)(s << 1) : (uint16_t)(s >> b);
}

void vp_project(void)
{
    static int16_t hcur[NVX], hprev[NVX];
    uint8_t r, k;
    int16_t dz;
    for (r = 0; r < NLINE; r++) {
        int16_t j = vp_j0 + VF_LINE(r), dx = vp_dx0;
        int32_t dzc, dzs;
        VfVtx *v = vf_v[r];
        dz = vp_dz0 + VF_LINE(r) * vp_cell;
        dzc = (int32_t)dz * vp_cp;
        dzs = (int32_t)dz * vp_sp;
        for (k = 0; k < NVX; k++) {
            int16_t i = vp_i0 + k;
            if (r < vp_r0 || j < 0 || j >= vp_n || i < 0 || i >= vp_n) hcur[k] = -32768;
            else {
                uint8_t p = (uint8_t)(j / vp_rpp);
                const uint8_t *q = host_page(vp_page + p) + (j - p * vp_rpp) * vp_rowb + i * 3;
                uint32_t t = (q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16) + vp_hbf;
                hcur[k] = (int16_t)(vp_hb12 + (int16_t)(t >> 12));
            }
        }
        for (k = 0; k < NVX; k++, dx += vp_cell) {
            int16_t dy, zp, yp, x;
            uint16_t s;
            int32_t y;
            if (hcur[k] != -32768 && (dx > vp_xlim[r] || dx < -vp_xlim[r])) hcur[k] = -32768;
            if (hcur[k] == -32768) { v[k].x = VF_BEHIND; continue; }
            dy = hcur[k] - vp_camy;
            zp = sh12(dzc - (int32_t)dy * vp_sp);
            if (zp < 12) { v[k].x = VF_BEHIND; continue; }
            if (zp < 21) zp = 21;
            yp = sh12((int32_t)dy * vp_cp + dzs);
            s = recip_s(zp);
            x = sh12((int32_t)dx * s);
            v[k].x = x < -384 ? -256 : x > 383 ? 511 : x + 128;
            y = 21472 - (((int32_t)yp * s) >> 5);
            v[k].y = y < 256 ? 256 : y > 65280L ? 65280u : (uint16_t)y;
        }
        if (r) {
            uint8_t *col = vf_col[r - 1];
            for (k = 0; k < NVX - 1; k++) {
                int16_t ha = hprev[k], hb = hprev[k + 1], hc = hcur[k + 1], hd = hcur[k], slope, pen;
                if (ha == -32768 || hb == -32768 || hc == -32768 || hd == -32768) { col[k] = VF_NONE; continue; }
                slope = ((ha + hd) - (hb + hc)) + ((ha + hb) - (hc + hd)) / (r >= NFINE ? 4 : 2);
                pen = 2 - slope / (int16_t)vp_cdiv;
                if (pen < 0) pen = 0;
                if (pen > 5) pen = 5;
                col[k] = (uint8_t)(vp_pen0 + pen);
            }
        }
        for (k = 0; k < NVX; k++) hprev[k] = hcur[k];
    }
}
#endif

#ifndef __SDCC
#include "vobj.h"
/* vobj.asm in C */
void vo_cam(int16_t dx, int16_t dy, int16_t dz, V3s *o)
{
    o->x = dx;
    o->y = sh12((int32_t)dy * vp_cp + (int32_t)dz * vp_sp);
    o->z = sh12((int32_t)dz * vp_cp - (int32_t)dy * vp_sp);
}

void vo_scr(const V3s *p, VfVtx *o)
{
    int16_t z = p->z < 21 ? 21 : p->z, x;
    uint16_t s = recip_s(z);
    int32_t y;
    x = sh12((int32_t)p->x * s);
    o->x = x < -384 ? -256 : x > 383 ? 511 : x + 128;
    y = 21472 - (((int32_t)p->y * s) >> 5);
    o->y = y < 256 ? 256 : y > 65280L ? 65280u : (uint16_t)y;
}

void vo_scr_n(const V3s *in, VfVtx *out, uint8_t n) { for (; n; n--) vo_scr(in++, out++); }

uint8_t vo_box(const int8_t *lh, const int16_t *k, const V3s *c, VfVtx *out, V3s *cam)
{
    int16_t t[3][2][3];
    uint8_t a, e, n, near = 0;
    for (a = 0; a < 3; a++)
        for (e = 0; e < 2; e++) {
            int16_t v = lh[e * 3 + a];
            t[a][e][0] = (int16_t)(((int32_t)k[a] * v) >> 12);
            t[a][e][1] = (int16_t)(((int32_t)k[3 + a] * v) >> 12);
            t[a][e][2] = (int16_t)(((int32_t)k[6 + a] * v) >> 12);
        }
    for (n = 0; n < 8; n++) {
        const int16_t *tx = t[0][n & 1], *ty = t[1][(n >> 1) & 1], *tz = t[2][n >> 2];
        cam[n].x = c->x + tx[0] + ty[0] + tz[0];
        cam[n].y = c->y + tx[1] + ty[1] + tz[1];
        cam[n].z = c->z + tx[2] + ty[2] + tz[2];
        if (cam[n].z < 12) near = 1;
    }
    if (!near) vo_scr_n(cam, out, 8);
    return near;
}

uint8_t vo_oct(const V3s *m, const int16_t *off, VfVtx *out, V3s *cam)
{
    static const int8_t pat[8][2] = { { 0, 1 }, { 1, 0 }, { 1, -1 }, { 0, -2 }, { -1, -2 }, { -2, -1 }, { -2, 0 }, { -1, 1 } };
    uint8_t k, near = 0;
    /* (pat: x 0 a, 1 b, -1 -a, -2 -b; z the same) */
    for (k = 0; k < 8; k++) {
        int8_t px = pat[k][0], pz = pat[k][1];
        int16_t x = px >= 0 ? off[px] : -off[-px - 1];
        int16_t ys = pz >= 0 ? off[2 + 2 * pz] : -off[2 + 2 * (-pz - 1)];
        int16_t zs = pz >= 0 ? off[3 + 2 * pz] : -off[3 + 2 * (-pz - 1)];
        cam[k].x = m->x + x; cam[k].y = m->y + ys; cam[k].z = m->z + zs;
        if (cam[k].z < 12) near = 1;
    }
    if (!near) vo_scr_n(cam, out, 8);
    return near;
}

void vo_model(const int8_t *v, uint8_t n, const int16_t *k, const V3s *c, V3s *out)
{
    for (; n; n--, v += 3, out++) {
        int32_t a = (int32_t)v[0] * k[0] + (int32_t)v[1] * k[1] + (int32_t)v[2] * k[2];
        int32_t b = (int32_t)v[0] * k[3] + (int32_t)v[1] * k[4] + (int32_t)v[2] * k[5];
        int32_t d = (int32_t)v[0] * k[6] + (int32_t)v[1] * k[7] + (int32_t)v[2] * k[8];
        out->x = (int16_t)(c->x + (a >> 12));
        out->y = (int16_t)(c->y + (b >> 12));
        out->z = (int16_t)(c->z + (d >> 12));
    }
}

/* vo_aff_box, vo_aff_pts: view.c's first-order map m = { mx[3], my[3], cx, cy } */
void vo_aff_pts(const int8_t *v, uint8_t n, const int16_t *m, VfVtx *out)
{
    for (; n; n--, v += 3, out++) {
        int16_t sx = (int16_t)(v[0] * m[0] + v[1] * m[1] + v[2] * m[2]);
        out->x = (int16_t)(m[6] + (sx >> 8));
        out->y = (uint16_t)(m[7] + v[0] * m[3] + v[1] * m[4] + v[2] * m[5]);
    }
}

void vo_aff_box(const int8_t *lohi, const int16_t *m, VfVtx *out)
{
    uint8_t c;
    for (c = 0; c < 8; c++) {
        int8_t v[3];
        v[0] = lohi[(c & 1) ? 3 : 0];
        v[1] = lohi[(c & 2) ? 4 : 1];
        v[2] = lohi[(c & 4) ? 5 : 2];
        vo_aff_pts(v, 1, m, &out[c]);
    }
}
#endif
