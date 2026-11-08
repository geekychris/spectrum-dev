/*
 * gen_levels.c - Lander 3D's levels for the Next, made with the original's
 * own code (tools/orig/ld_core.c on the amiga3d engine): for each world and
 * level 1..LV_PER_WORLD, ld_new_level() with its seed and the autopilot's
 * probe flight (the fuel top-up), then the result written for the Next:
 *
 *   build/levels/bank_NN.bin   16K banks from LEVEL_BANK: first the level
 *                              records (lv_rec.h's layout, LV_REC_SIZE bytes
 *                              each), then each level's heights: rows of n
 *                              3-byte heights (16.16 metres above the
 *                              record's hbase, low byte first), `rpp` new rows a
 *                              8K page and the next page's first row again
 *                              at the end (a cell's two rows are always in
 *                              one page)
 *   build/levels/expect.txt    the original's state hashes of autopilot
 *                              flights (tools/check_core.sh, test.py)
 *
 * Built and run by the Makefile (gcc in the toolchain image).
 *
 *   gen_levels OUTDIR
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ld.h"
#include "../src/lv_rec.h"
#include "../src/flight_hash.h"

static Level L;
static unsigned char banks[LEVEL_BANKS * 16384];
static int npages;

static void put32(unsigned char *p, int32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static void put_level(int idx, int w, int lv)
{
    unsigned char *rec = banks + idx * LV_REC_SIZE;
    const Terrain *T = &L.T;
    int n = T->n, rowb = n * 3, rpp = 8192 / rowb - 1, j, i, k = 0, page0 = npages;
    fix hmax = -0x7FFFFFFF, hmin = 0x7FFFFFFF;
    int32_t v[LV_NFIELDS];

    for (j = 0; j < n * n; j++) {
        if (T->h[j] > hmax) hmax = T->h[j];
        if (T->h[j] < hmin) hmin = T->h[j];
    }
    /* the heights: page p holds rows rpp * p .. rpp * p + rpp */
    for (j = 0; j < n; j += rpp) {
        unsigned char *pg = banks + (size_t)npages * 8192;
        int r;
        for (r = 0; r <= rpp && j + r < n; r++)
            for (i = 0; i < n; i++) {
                int32_t h = T->h[(j + r) * n + i] - hmin;
                if (h >= (1 << 24)) { fprintf(stderr, "height range over 24 bits\n"); exit(1); }
                pg[r * rowb + i * 3] = (unsigned char)h;
                pg[r * rowb + i * 3 + 1] = (unsigned char)(h >> 8);
                pg[r * rowb + i * 3 + 2] = (unsigned char)(h >> 16);
            }
        npages++;
        if (j + rpp >= n - 1) break;            /* the last page has the last row */
    }
    /* each row's highest and lowest heights in engine units (>> 12), int16,
       at RANGE_OFF in the last page: what the view culls by */
    {
        unsigned char *pg = banks + (size_t)(npages - 1) * 8192 + LV_RANGE_OFF;
        int r = 0;
        if (n * 4 + LV_RANGE_OFF > 8192 || (n - 1 - (n - 1) / rpp * rpp + 1) * rowb > LV_RANGE_OFF) { fprintf(stderr, "no room for the ranges\n"); exit(1); }
        for (j = 0; j < n; j++, r++) {
            fix lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
            for (i = 0; i < n; i++) {
                if (T->h[j * n + i] > hi) hi = T->h[j * n + i];
                if (T->h[j * n + i] < lo) lo = T->h[j * n + i];
            }
            pg[j * 2] = (unsigned char)(hi >> 12); pg[j * 2 + 1] = (unsigned char)((hi >> 12) >> 8);
            pg[n * 2 + j * 2] = (unsigned char)(lo >> 12); pg[n * 2 + j * 2 + 1] = (unsigned char)((lo >> 12) >> 8);
        }
    }
#define F(x) v[k++] = (int32_t)(x)
    F(w); F(lv); F(L.seed);
    F(L.params.fuel); F(L.params.start_alt); F(L.params.start_h); F(L.params.start_dist); F(L.params.pad_scale); F(L.params.wind);
    F(T->n); F(T->size); F(T->cell); F(T->half); F(T->inv_cell);
    for (i = 0; i < 3; i++) { F(T->pads[i].x); F(T->pads[i].z); F(T->pads[i].r); F(T->pads[i].y); F(T->pads[i].mult); }
    for (i = 0; i < 3; i++) F(L.c.p[i]);
    for (i = 0; i < 3; i++) F(L.c.v[i]);
    F(L.c.pitch); F(L.c.roll); F(L.c.wp); F(L.c.wr); F(L.c.throttle);
    F(L.fuel); F(L.fuel_max); F(L.wind_dir); F(L.wind_ph); F(L.probe_used); F(L.probe_outcome);
    F(hmax); F(hmin); F(rpp); F(rowb); F(LEVEL_BANK * 2 + page0); F(LEVEL_BANK * 2 + npages - 1);
#undef F
    if (k != LV_NFIELDS) { fprintf(stderr, "%d fields, lv_rec.h says %d\n", k, LV_NFIELDS); exit(1); }
    for (i = 0; i < k; i++) put32(rec + i * 4, v[i]);
}

int main(int argc, char **argv)
{
    char path[512];
    FILE *f;
    int w, lv, b;
    if (argc < 2) { fprintf(stderr, "usage: gen_levels OUTDIR\n"); return 2; }
    npages = LV_HDR_PAGES;
    for (w = 0; w < W_COUNT; w++)
        for (lv = 1; lv <= LV_PER_WORLD; lv++) {
            ld_new_level(&L, w, lv, LV_SEED(w, lv), 1);
            if (L.probe_outcome != OUT_LANDED) fprintf(stderr, "warning: %s %d: the probe didn't land\n", ld_worlds[w].name, lv);
            put_level(w * LV_PER_WORLD + lv - 1, w, lv);
            printf("%-8s %2d seed %d fuel %5.1f / %5.1f (probe %5.1f)%s pages %d\n", ld_worlds[w].name, lv, LV_SEED(w, lv),
                   L.fuel / 65536.0, L.fuel_max / 65536.0, L.probe_used / 65536.0,
                   L.fuel_max > L.params.fuel ? " topped up" : "", npages);
        }
    if (npages > LEVEL_BANKS * 2) { fprintf(stderr, "%d pages, more than LEVEL_BANKS * 2\n", npages); return 1; }
    for (b = 0; b < LEVEL_BANKS; b++) {
        snprintf(path, sizeof(path), "%s/bank_%d.bin", argv[1], LEVEL_BANK + b);
        if (!(f = fopen(path, "wb")) || fwrite(banks + b * 16384, 1, 16384, f) != 16384) { perror(path); return 1; }
        fclose(f);
    }
    /* the original's hashes of autopilot flights (see flight_hash.h) */
    snprintf(path, sizeof(path), "%s/expect.txt", argv[1]);
    if (!(f = fopen(path, "w"))) { perror(path); return 1; }
    for (w = 0; w < W_COUNT; w++) {
        static const int lvs[3] = { 1, 5, 10 };
        int k, kind;
        for (kind = 0; kind < 2; kind++)
            for (k = 0; k < 3; k++) {
                uint32_t h;
                int32_t steps;
                ld_new_level(&L, w, lvs[k], LV_SEED(w, lvs[k]), 1);
                h = flight_hash_kind(&L, kind, FLIGHT_STEPS, &steps);
                fprintf(f, "flight %d %d %d %ld %ld %ld %08lX\n", kind, w, lvs[k], (long)steps, (long)L.outcome,
                        (long)L.result.score, (unsigned long)h);
            }
    }
    fclose(f);
    printf("%d pages in banks %d-%d\n", npages, LEVEL_BANK, LEVEL_BANK + (npages + 1) / 2 - 1);
    return 0;
}
