/* Convex polygons on 320 x 256 column-major planes, with a depth plane (see l2wpoly.h). */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "l2wpoly.h"
#include "nextgfx.h"

#define DMA_MIN 40                      /* spans this long go by DMA (~2 T-states a byte + ~450), shorter by LDIR (21) */

uint8_t l2wp_key = 0;
uint8_t l2wp_tslot = 3, l2wp_trestore = 11, l2wp_zslot = 0, l2wp_zrestore = 0xFF;

static uint8_t tpage, zpage;
static int16_t cx0, cy0, cx1 = 320, cy1 = 256;
/* per column of the polygon being drawn: the upper and lower edge */
static int16_t top[320], bot[320];

#define TWIN ((uint8_t *)((uint16_t)l2wp_tslot << 13))
#define ZWIN ((uint8_t *)((uint16_t)l2wp_zslot << 13))
#define TMAP(p) ZXN_WRITE_REG(0x50 + l2wp_tslot, p)
#define ZMAP(p) ZXN_WRITE_REG(0x50 + l2wp_zslot, p)

void l2wp_slots(uint8_t tslot, uint8_t trestore, uint8_t zslot, uint8_t zrestore)
{
    l2wp_tslot = tslot;
    l2wp_trestore = trestore;
    l2wp_zslot = zslot;
    l2wp_zrestore = zrestore;
}

void l2wp_target(uint8_t page, uint8_t zp)
{
    tpage = page;
    zpage = zp;
}

void l2wp_clip(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    cx0 = x0 < 0 ? 0 : x0;
    cy0 = y0 < 0 ? 0 : y0;
    cx1 = x1 > 320 ? 320 : x1;
    cy1 = y1 > 256 ? 256 : y1;
}

static void done(void)
{
    ZMAP(l2wp_zrestore);
    TMAP(l2wp_trestore);
}

/* columns x0 .. x1-1 from top[] to bot[], clipped rows */
static void spans(int16_t x0, int16_t x1, uint8_t colour)
{
    uint8_t page = 0xFF, p;
    int16_t x, y0, y1;
    for (x = x0; x < x1; x++) {
        y0 = top[x];
        y1 = bot[x];
        if (y0 < cy0) y0 = cy0;
        if (y1 > cy1) y1 = cy1;
        if (y0 >= y1) continue;
        p = (uint8_t)(x >> 5);
        if (p != page) {
            page = p;
            TMAP(tpage + p);
            if (zpage) ZMAP(zpage + p);
        }
        {
            uint16_t off = ((uint16_t)(x & 31) << 8) + (uint16_t)y0;
            uint16_t n = (uint16_t)(y1 - y0);
            if (n >= DMA_MIN) {
                dma_fill(TWIN + off, colour, n);
                if (zpage) dma_fill(ZWIN + off, l2wp_key, n);
            } else {
                memset(TWIN + off, colour, n);
                if (zpage) memset(ZWIN + off, l2wp_key, n);
            }
        }
    }
    done();
}

void l2wp_poly(const int16_t *xy, uint8_t n, uint8_t colour)
{
    int16_t xmin = 32767, xmax = -32768, x, xa, ya, xb, yb, lo, hi;
    uint8_t i;
    if (n < 3 || n > L2WP_MAXV) return;
    for (i = 0; i < n; i++) {
        x = xy[i * 2];
        if (x < xmin) xmin = x;
        if (x > xmax) xmax = x;
    }
    lo = xmin < cx0 ? cx0 : xmin;
    hi = xmax > cx1 ? cx1 : xmax;
    if (lo >= hi) return;
    for (x = lo; x < hi; x++) { top[x] = 32767; bot[x] = -32768; }
    for (i = 0; i < n; i++) {
        uint8_t j = i + 1 == n ? 0 : i + 1;
        int32_t y, dy;
        int16_t s, e, v;
        xa = xy[i * 2]; ya = xy[i * 2 + 1];
        xb = xy[j * 2]; yb = xy[j * 2 + 1];
        if (xa == xb) continue;
        if (xa > xb) { x = xa; xa = xb; xb = x; x = ya; ya = yb; yb = x; }
        dy = ((int32_t)(yb - ya) << 16) / (xb - xa);
        s = xa < lo ? lo : xa;
        e = xb > hi ? hi : xb;
        y = ((int32_t)ya << 16) + dy * (s - xa) + 0x8000L;
        for (x = s; x < e; x++, y += dy) {
            v = (int16_t)(y >> 16);
            if (v < top[x]) top[x] = v;
            if (v > bot[x]) bot[x] = v;
        }
    }
    spans(lo, hi, colour);
}

void l2wp_rect(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t colour)
{
    int16_t x;
    if (x0 < cx0) x0 = cx0;
    if (x1 > cx1) x1 = cx1;
    if (x0 >= x1) return;
    for (x = x0; x < x1; x++) { top[x] = y0; bot[x] = y1; }
    spans(x0, x1, colour);
}

void l2wp_copy(uint8_t dst, uint8_t src, int16_t x0, int16_t y0, int16_t x1, int16_t y1)
{
    uint8_t page = 0xFF, p;
    int16_t x;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 320) x1 = 320;
    if (y1 > 256) y1 = 256;
    if (x0 >= x1 || y0 >= y1) return;
    for (x = x0; x < x1; x++) {
        uint16_t off = ((uint16_t)(x & 31) << 8) + (uint16_t)y0;
        p = (uint8_t)(x >> 5);
        if (p != page) {
            page = p;
            TMAP(dst + p);
            ZMAP(src + p);
        }
        dma_copy(TWIN + off, ZWIN + off, (uint16_t)(y1 - y0));
    }
    done();
}

void l2wp_fill(uint8_t page0, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t v)
{
    uint8_t page = 0xFF, p;
    int16_t x;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 320) x1 = 320;
    if (y1 > 256) y1 = 256;
    if (x0 >= x1 || y0 >= y1) return;
    for (x = x0; x < x1; x++) {
        p = (uint8_t)(x >> 5);
        if (p != page) { page = p; TMAP(page0 + p); }
        dma_fill(TWIN + ((uint16_t)(x & 31) << 8) + (uint16_t)y0, v, (uint16_t)(y1 - y0));
    }
    done();
}
