/* Voxel-space terrain (see voxel.h). The set-up per slice is C; the loop
   over a slice's columns is vox_slice(): C here (the reference, host
   builds, or VOX_C on the Next), else voxel_draw.asm. */
#include <stdint.h>
#include <string.h>
#include "voxel.h"
#include "fix16.h"

uint8_t vox_ybuf[VOX_COLS];
uint8_t vox_snap[VOX_SNAPS][VOX_COLS];
#ifndef __SDCC
uint8_t *vox_host_fb;
#endif

/* what the slice loop reads (voxel_draw.asm knows this layout: keep it in step) */
struct VoxState {
    const int8_t *heights;                  /* +0 */
    const uint8_t *colours;                 /* +2 */
    const int16_t *mid;                     /* +4: the ribbon, or 0 */
    const uint8_t *stripe;                  /* +6 */
    uint16_t x, z, sx, sz;                  /* +8: column 0's position and the step, cells * 256 */
    int16_t a, b;                           /* +16: row = a - (height * b >> 8) */
    uint8_t csh, nbands;                    /* +20 */
    VoxBand bands[VOX_BANDS];               /* +22 */
} vox_s;

static uint8_t hshift, hscale, rows;
static int32_t camx, camy, camz;
static int16_t fx, fz;                      /* the heading, Q14 */
static int16_t fovx, fovy, horizon;
static int16_t slice_d[VOX_SLICES];
static uint8_t nslices;
static int16_t snap_d[VOX_SNAPS];

void vox_world(uint8_t hs, uint8_t sc) { hshift = hs; hscale = sc; }

void vox_maps(const int8_t *heights, const uint8_t *colours, uint8_t csh)
{
    vox_s.heights = heights;
    vox_s.colours = colours;
    vox_s.csh = csh;
}

void vox_ribbon(const int16_t *mid, const uint8_t *stripe, const VoxBand *bands, uint8_t n)
{
    vox_s.mid = mid;
    vox_s.stripe = stripe;
    if (n > VOX_BANDS) n = VOX_BANDS;
    vox_s.nbands = n;
    if (n) memcpy(vox_s.bands, bands, n * sizeof(VoxBand));
}

void vox_camera(int32_t x, int32_t y, int32_t z, uint16_t yaw)
{
    camx = x;
    camy = y;
    camz = z;
    fx = (int16_t)(a3_sin(yaw) >> 2);
    fz = (int16_t)(a3_cos(yaw) >> 2);
}

void vox_lens(int16_t fx_, int16_t fy_, int16_t hz, uint8_t r)
{
    fovx = fx_;
    fovy = fy_;
    horizon = hz;
    rows = r;
}

void vox_depth(int16_t near, int16_t far, uint8_t grow)
{
    int16_t d = near;
    nslices = 0;
    while (d <= far && nslices < VOX_SLICES) {
        slice_d[nslices++] = d;
        d += 1 + d / grow;
    }
}

void vox_snap_at(uint8_t k, int16_t depth)
{
    if (k < VOX_SNAPS) snap_d[k] = depth;
}

/* ---- the fill: rows y0 <= y < y1 of pixel columns 2c and 2c + 1 ---- */
#ifdef __SDCC
extern void vox_fill(uint8_t c, uint8_t y0, uint8_t y1, uint8_t pen);    /* voxel_draw.asm */
extern void vox_l2_begin(void);
extern void vox_l2_end(void);
#else
static void vox_fill(uint8_t c, uint8_t y0, uint8_t y1, uint8_t pen)
{
    uint8_t *p = vox_host_fb + (uint16_t)y0 * 256 + 2 * c;
    for (; y0 < y1; y0++, p += 256) p[0] = p[1] = pen;
}
#define vox_l2_begin()
#define vox_l2_end()
#endif

/* a point's pen: the ribbon's band it is in, else the colour map's */
static uint8_t pen_at(uint16_t x, uint16_t z)
{
    uint8_t cx = (uint8_t)(x >> 8) & (VOX_MAP - 1), cz = (uint8_t)(z >> 8) & (VOX_MAP - 1), k;
    if (vox_s.mid) {
        int16_t d = (int16_t)(x - (uint16_t)vox_s.mid[cz]);
        uint8_t left = d < 0;
        if (left) d = -d;
        for (k = 0; k < vox_s.nbands; k++)
            if (d < vox_s.bands[k].half)
                return vox_s.bands[k].pen[(vox_s.stripe[cz] ^ (vox_s.bands[k].sided & left)) & 1];
    }
    return vox_s.colours[(uint16_t)(cz >> vox_s.csh) << (7 - vox_s.csh) | (cx >> vox_s.csh)];
}

/* one slice across every column (the reference) */
#if defined(__SDCC) && !defined(VOX_C)
extern void vox_slice(void);
#else
static void vox_slice(void)
{
    uint16_t x = vox_s.x, z = vox_s.z;
    uint8_t c, *yb = vox_ybuf;
    for (c = 0; c < VOX_COLS; c++, yb++, x += vox_s.sx, z += vox_s.sz) {
        int8_t h;
        int16_t y;
        if (!*yb) continue;
        h = vox_s.heights[(uint16_t)((uint8_t)(z >> 8) & (VOX_MAP - 1)) << 7 | ((uint8_t)(x >> 8) & (VOX_MAP - 1))];
        y = vox_s.a - (int16_t)(((int32_t)h * vox_s.b) >> 8);
        if (y < 0) y = 0;
        if (y >= *yb) continue;
        vox_fill(c, (uint8_t)y, *yb, pen_at(x, z));
        *yb = (uint8_t)y;
    }
}
#endif

uint8_t vox_draw(void)
{
    uint8_t k, j, taken = 0;
    int16_t rx = fz, rz = -fx;                     /* to the right */
    memset(vox_ybuf, rows, VOX_COLS);
    vox_l2_begin();
    for (k = 0; k < nslices; k++) {
        int16_t d = slice_d[k];
        int32_t xr0 = (int32_t)(1 - VOX_COLS) * d * 256 / fovx;   /* column 0's offset to the right, world * 256 */
        int32_t step = (int32_t)2 * d * 256 / fovx;
        int16_t kk = (int16_t)(((int32_t)fovy << 8) / d);          /* rows a unit up, * 256 */
        for (j = 0; j < VOX_SNAPS; j++)
            if (snap_d[j] && !(taken & (1 << j)) && snap_d[j] <= d) {
                memcpy(vox_snap[j], vox_ybuf, VOX_COLS);
                taken |= (uint8_t)(1 << j);
            }
        /* world * 256 -> cells * 256: >> hshift */
        /* (the heading to Q12 here: xr0 * Q14 overflows 32 bits far out) */
        vox_s.x = (uint16_t)(((camx << 8) + (((int32_t)d * fx) >> 6) + ((xr0 * (rx >> 2)) >> 12)) >> hshift);
        vox_s.z = (uint16_t)(((camz << 8) + (((int32_t)d * fz) >> 6) + ((xr0 * (rz >> 2)) >> 12)) >> hshift);
        vox_s.sx = (uint16_t)((step * rx) >> (14 + hshift));
        vox_s.sz = (uint16_t)((step * rz) >> (14 + hshift));
        vox_s.a = horizon + (int16_t)((camy * kk) >> 8);
        vox_s.b = kk << hscale;
        vox_slice();
    }
    for (j = 0; j < VOX_SNAPS; j++)
        if (snap_d[j] && !(taken & (1 << j))) memcpy(vox_snap[j], vox_ybuf, VOX_COLS);
    vox_l2_end();
    return nslices;
}

void vox_sky(uint8_t pen)
{
    uint8_t c;
    vox_l2_begin();
    for (c = 0; c < VOX_COLS; c++)
        if (vox_ybuf[c]) vox_fill(c, 0, vox_ybuf[c], pen);
    vox_l2_end();
}

uint8_t vox_project(int32_t x, int32_t y, int32_t z, int16_t *sx, int16_t *sy, int16_t *depth)
{
    int32_t dx = x - camx, dz = z - camz, d, lat;
    d = (dx * fx + dz * fz) >> 14;
    lat = (dx * fz - dz * fx) >> 14;
    if (d < slice_d[0]) return 0;
    *depth = (int16_t)d;
    *sx = (int16_t)(128 + lat * fovx / d);
    *sy = (int16_t)(horizon - (y - camy) * fovy / d);
    return 1;
}
