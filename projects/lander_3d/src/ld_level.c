// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Chris Collins <chris@hitorro.com>

/*
 * ld_level.c - a level's start from its record (the original's
 * ld_new_level(); see ld.h). In bank 24 with the text (once a level; bank
 * 20 is full): ter.c's ld_new_level() maps it in, then clears the rules'
 * kept answers. On the host ld_new_level() is here.
 */
#include <string.h>
#include "ld.h"
#include "lv_rec.h"
#include "ld_int.h"

/* ------------------------------------------------------------ levels */
/* port: the original makes the level here (ld_make_terrain, the start, the
   probe flight that sizes the tank); the Next reads what the original's
   code made on the host (tools/gen_levels.c) */
void ld_level_load(Level *L, int world, int level)
{
    uint8_t r, k;
    int lv = level < 1 ? 1 : level;
    if (lv > LV_PER_WORLD) lv = LV_PER_WORLD - 1 + (lv - LV_PER_WORLD + 1) % 2;   /* round the last two */
    r = (uint8_t)(world * LV_PER_WORLD + lv - 1);
    memset(L, 0, sizeof(*L));
    L->world = world;
    L->level = level;
    L->seed = (uint32_t)lv_field(r, LVF_SEED);
    for (k = 0; k < 6; k++) (&L->params.fuel)[k] = lv_field(r, LVF_PARAMS + k);
    L->T.world = world;
    L->T.n = (int)lv_field(r, LVF_N);
    L->T.size = (int)lv_field(r, LVF_SIZE);
    L->T.cell = lv_field(r, LVF_CELL);
    L->T.half = lv_field(r, LVF_HALF);
    L->T.inv_cell = lv_field(r, LVF_INV_CELL);
    for (k = 0; k < 3; k++) {
        Pad *p = &L->T.pads[k];
        p->x = lv_field(r, LVF_PADS + k * 5);
        p->z = lv_field(r, LVF_PADS + k * 5 + 1);
        p->r = lv_field(r, LVF_PADS + k * 5 + 2);
        p->y = lv_field(r, LVF_PADS + k * 5 + 3);
        p->mult = (int)lv_field(r, LVF_PADS + k * 5 + 4);
    }
    L->T.hmax = lv_field(r, LVF_HMAX);
    L->T.hbase = lv_field(r, LVF_HBASE);
    L->T.rpp = (uint8_t)lv_field(r, LVF_RPP);
    L->T.rowb = (uint16_t)lv_field(r, LVF_ROWB);
    L->T.page = (uint8_t)lv_field(r, LVF_PAGE);
    L->T.range_page = (uint8_t)lv_field(r, LVF_RANGE_PAGE);
    for (k = 0; k < 3; k++) {
        L->c.p[k] = lv_field(r, LVF_P + k);
        L->c.v[k] = lv_field(r, LVF_V + k);
    }
    L->c.pitch = lv_field(r, LVF_PITCH);
    L->c.roll = lv_field(r, LVF_ROLL);
    L->c.wp = lv_field(r, LVF_WP);
    L->c.wr = lv_field(r, LVF_WR);
    L->c.throttle = lv_field(r, LVF_THROTTLE);
    L->fuel = lv_field(r, LVF_FUEL);
    L->fuel_max = lv_field(r, LVF_FUEL_MAX);
    L->wind_dir = lv_field(r, LVF_WIND_DIR);
    L->wind_ph = lv_field(r, LVF_WIND_PH);
    L->probe_used = lv_field(r, LVF_PROBE_USED);
    L->probe_outcome = (int)lv_field(r, LVF_PROBE_OUTCOME);
    L->assist = 1;
    L->ap_target = -1;
}

#ifndef __SDCC
void ld_new_level(Level *L, int world, int level)
{
    ld_level_load(L, world, level);
    ld_caches_reset();
}
#endif
