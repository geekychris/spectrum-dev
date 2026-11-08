/*
 * ter.c - the level records and the terrain's heights in the memory pages
 * the .nex loads them into (lv_rec.h, made by tools/gen_levels.c), read
 * through MMU slot 2 (0x4000-0x5FFF; the tilemap's page otherwise: ter_done
 * puts it back). Resident: the rules (bank 20) and the view (bank 21) call
 * it. tools/host_ter.c is the same on the host.
 */
#include <arch/zxn.h>
#include "ld.h"
#include "lv_rec.h"

#define SLOT2 ((const uint8_t *)0x4000)
static uint8_t mapped = 0xFF;

static void map(uint8_t page)
{
    if (page != mapped) {
        ZXN_WRITE_MMU2(page);
        mapped = page;
    }
}

void ter_done(void)
{
    ZXN_WRITE_MMU2(10);
    mapped = 0xFF;
}

int32_t lv_field(uint8_t r, uint8_t f)
{
    map(LEVEL_BANK * 2);
    return *(const int32_t *)(SLOT2 + (uint16_t)r * LV_REC_SIZE + f * 4);
}

/* row j's first byte, its page mapped (rows j and j + 1 are in one page) */
static const uint8_t *row(const Terrain *T, uint8_t j)
{
    uint8_t p = j / T->rpp;
    map(T->page + p);
    return SLOT2 + (uint16_t)(uint8_t)(j - p * T->rpp) * T->rowb;
}

static fix at(const Terrain *T, const uint8_t *q)
{
    uint32_t v = q[0] | (uint16_t)q[1] << 8 | (uint32_t)q[2] << 16;
    return T->hbase + (fix)v;
}

fix ter_h(const Terrain *T, uint8_t i, uint8_t j)
{
    return at(T, row(T, j) + (uint16_t)i * 3);
}

void ter_cell(const Terrain *T, uint8_t i, uint8_t j, fix h[4])
{
    const uint8_t *q = row(T, j) + (uint16_t)i * 3;
    h[0] = at(T, q);
    h[1] = at(T, q + 3);
    q += T->rowb;
    h[2] = at(T, q);
    h[3] = at(T, q + 3);
}
