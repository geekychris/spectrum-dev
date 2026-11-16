/*
 * ground.c - the valley as the voxel renderer's maps (sdk voxel.h), streamed
 * in around the camera as it moves.
 *
 *   heights   the original's ground (rr_height) on a lattice of points 64
 *             units apart, cached in a 32 x 32 ring, spread bilinearly over
 *             128 x 128 height cells of 16 units (a quarter of the height
 *             in each byte)
 *   colours   one pen per lattice cell, by its slope: the original shades
 *             each ground quad so (render.c's ground())
 *   ribbon    the track: the original's course middle (rr_centre) for each
 *             row of height cells, the stripes every 80 units
 *
 * port: the original's ground cells are 80 units; here they are 64 (the
 * renderer's maps are powers of two), and the slope that picks a shade is
 * scaled to match (/ 19, not / 24).
 */
#include <string.h>
#include "ground.h"
#include "view.h"

#define LAT     GROUND_LAT
#define NP      32                      /* lattice points held a side (31 cells) */
#define SUB     (LAT >> GROUND_SHIFT)   /* height cells a lattice cell: 4 */
#define BACK    15                      /* points kept behind the camera's lattice point */

int8_t ground_h[128 * 128];
uint8_t ground_pen[32 * 32];
int16_t ground_mid[128];
uint8_t ground_stripe[128];

static int16_t lat[NP][NP];             /* [j & 31][i & 31] */
static int16_t li, lj;                  /* the first lattice point held, each way */
static int16_t rlo;                     /* the first height row with its ribbon set */

static int16_t floor_div(int32_t a, int16_t b) { return (int16_t)(a >= 0 ? a / b : -((-a + b - 1) / b)); }

static void point(const RRGame *g, int16_t i, int16_t j)
{
    lat[j & (NP - 1)][i & (NP - 1)] = (int16_t)rr_height(g, (int32_t)i * LAT, (int32_t)j * LAT);
}

/* lattice cell (i, j): its pen and its 4 x 4 height cells */
static void cell(int16_t i, int16_t j)
{
    int16_t ha = lat[j & (NP - 1)][i & (NP - 1)], hb = lat[j & (NP - 1)][(i + 1) & (NP - 1)];
    int16_t hc = lat[(j + 1) & (NP - 1)][(i + 1) & (NP - 1)], hd = lat[(j + 1) & (NP - 1)][i & (NP - 1)];
    int16_t slope = ((ha + hd) - (hb + hc)) + ((hc + hd) - (ha + hb)) / 2, pen = 2 + slope / 19;
    uint8_t a, b;
    if (pen < 0) pen = 0;
    if (pen > 5) pen = 5;
    ground_pen[(j & 31) * 32 + (i & 31)] = (uint8_t)(PEN_GROUND + pen);
    for (b = 0; b < SUB; b++) {
        /* along z from the a-b edge to the d-c edge, then across */
        int16_t l = ha + (int16_t)((int32_t)(hd - ha) * b / SUB), r = hb + (int16_t)((int32_t)(hc - hb) * b / SUB);
        int8_t *row = &ground_h[(uint16_t)(((uint8_t)(j * SUB + b)) & 127) << 7];
        for (a = 0; a < SUB; a++) {
            int16_t h = (l + (int16_t)((int32_t)(r - l) * a / SUB)) >> 2;
            row[(uint8_t)(i * SUB + a) & 127] = (int8_t)(h < -128 ? -128 : h > 127 ? 127 : h);
        }
    }
}

static void ribbon_row(const RRGame *g, int16_t r)
{
    int32_t z = (int32_t)r << GROUND_SHIFT;
    uint8_t k = (uint8_t)r & 127;
    ground_mid[k] = (int16_t)(rr_centre(g, z + (1 << (GROUND_SHIFT - 1))) << (8 - GROUND_SHIFT));
    ground_stripe[k] = (uint8_t)(floor_div(z, 80) & 1);           /* the original's stripes, a SEG apart */
}

void ground_reset(const RRGame *g, int32_t x, int32_t z)
{
    int16_t i, j;
    li = floor_div(x, LAT) - BACK;
    lj = floor_div(z, LAT) - BACK;
    memset(ground_pen, 99, sizeof(ground_pen));         /* (debug: unfilled) */
    for (j = lj; j < lj + NP; j++)
        for (i = li; i < li + NP; i++) point(g, i, j);
    for (j = lj; j < lj + NP - 1; j++)
        for (i = li; i < li + NP - 1; i++) cell(i, j);
    rlo = lj * SUB;
    for (j = rlo; j < rlo + 128; j++) ribbon_row(g, j);
}

uint8_t ground_follow(const RRGame *g, int32_t x, int32_t z)
{
    int16_t wi = floor_div(x, LAT) - BACK, wj = floor_div(z, LAT) - BACK, k;
    uint8_t did = 0;
    if (wi > li + 2 || wi < li - 2 || wj > lj + 2 || wj < lj - 2) {   /* a jump: start again */
        ground_reset(g, x, z);
        return 1;
    }
    /* one column and one row of points a call at most (the camera moves
       less than a lattice cell a tick) */
    if (wi > li) {
        for (k = lj; k < lj + NP; k++) point(g, li + NP, k);
        for (k = lj; k < lj + NP - 1; k++) cell(li + NP - 1, k);
        li++;
        did = 1;
    } else if (wi < li) {
        li--;
        for (k = lj; k < lj + NP; k++) point(g, li, k);
        for (k = lj; k < lj + NP - 1; k++) cell(li, k);
        did = 1;
    }
    if (wj > lj) {
        for (k = li; k < li + NP; k++) point(g, k, lj + NP);
        for (k = li; k < li + NP - 1; k++) cell(k, lj + NP - 1);
        lj++;
        did = 1;
    } else if (wj < lj) {
        lj--;
        for (k = li; k < li + NP; k++) point(g, k, lj);
        for (k = li; k < li + NP - 1; k++) cell(k, lj);
        did = 1;
    }
    /* the ribbon's rows follow the height rows */
    while (rlo < lj * SUB) { ribbon_row(g, rlo + 128); rlo++; did = 1; }
    while (rlo > lj * SUB) { rlo--; ribbon_row(g, rlo); did = 1; }
    return did;
}
