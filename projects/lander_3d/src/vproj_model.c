/* vproj_model.c - vproj.asm's steps in C, for the host build (the same
   arithmetic: the 12-bit shifts clamped to 16 bits, the reciprocal table,
   the clamps), so the host's pictures are the Next's */
#ifndef __SDCC
#include "ld.h"
#include "vfill.h"
#include "vproj.h"

int16_t vp_dx0, vp_dz0, vp_cell, vp_camy, vp_cp, vp_sp, vp_i0, vp_j0, vp_hb12;
uint8_t vp_n, vp_page, vp_rpp, vp_pen0;
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
    int16_t dz = vp_dz0;
    for (r = 0; r < NLINE; r++, dz += vp_cell) {
        int16_t j = vp_j0 + r, dx = vp_dx0;
        int32_t dzc = (int32_t)dz * vp_cp, dzs = (int32_t)dz * vp_sp;
        VfVtx *v = vf_v[r];
        for (k = 0; k < NVX; k++) {
            int16_t i = vp_i0 + k;
            if (j < 0 || j >= vp_n || i < 0 || i >= vp_n) hcur[k] = -32768;
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
                slope = ((ha + hd) - (hb + hc)) + ((ha + hb) - (hc + hd)) / 2;
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
#endif
