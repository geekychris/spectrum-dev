/*
 * host_ter.c - src/ter.c on the host: the level records and heights from
 * the banks gen_levels wrote (build/levels/bank_NN.bin), read the same way
 * (pages, rows, 3-byte heights), so host_check tests the data's layout too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ld.h"
#include "lv_rec.h"

static uint8_t mem[LEVEL_BANKS * 16384];
static int loaded;

void host_ter_load(const char *dir)
{
    char path[512];
    int b;
    for (b = 0; b < LEVEL_BANKS; b++) {
        FILE *f;
        snprintf(path, sizeof(path), "%s/bank_%d.bin", dir, LEVEL_BANK + b);
        if (!(f = fopen(path, "rb")) || fread(mem + b * 16384, 1, 16384, f) != 16384) { perror(path); exit(1); }
        fclose(f);
    }
    loaded = 1;
}

static const uint8_t *page(int p) { return mem + (p - LEVEL_BANK * 2) * 8192; }
const uint8_t *host_page(uint8_t p) { return page(p); }

int32_t lv_field(uint8_t r, uint8_t f)
{
    const uint8_t *q = page(LEVEL_BANK * 2) + r * LV_REC_SIZE + f * 4;
    return (int32_t)((uint32_t)q[0] | (uint32_t)q[1] << 8 | (uint32_t)q[2] << 16 | (uint32_t)q[3] << 24);
}

void ter_done(void) {}

static const uint8_t *row(const Terrain *T, uint8_t j)
{
    int p = j / T->rpp;
    return page(T->page + p) + (j - p * T->rpp) * T->rowb;
}

static fix at(const Terrain *T, const uint8_t *q) { return T->hbase + (fix)(q[0] | q[1] << 8 | q[2] << 16); }

fix ter_h(const Terrain *T, uint8_t i, uint8_t j) { return at(T, row(T, j) + i * 3); }

void ter_cell(const Terrain *T, uint8_t i, uint8_t j, fix h[4])
{
    const uint8_t *q = row(T, j) + i * 3;
    h[0] = at(T, q); h[1] = at(T, q + 3);
    q += T->rowb;
    h[2] = at(T, q); h[3] = at(T, q + 3);
}

void ter_range(const Terrain *T, uint8_t j, int16_t *hi, int16_t *lo)
{
    const uint8_t *q = page(T->range_page) + LV_RANGE_OFF;
    *hi = (int16_t)(q[j * 2] | q[j * 2 + 1] << 8);
    *lo = (int16_t)(q[(T->n + j) * 2] | q[(T->n + j) * 2 + 1] << 8);
}
