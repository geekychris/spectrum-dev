/*
 * lv_rec.h - the levels made on the host (tools/gen_levels.c, with the
 * original's code): where they are in memory and how a record is laid out.
 * Shared by the generator and the Next.
 *
 * port: the original makes a level's ground, pads and start from a seed
 * when the level starts (and a new seed for every try), then flies the
 * autopilot over it in the background to size the tank. On the Next that
 * would take ~16 s of ground and ~50 s of autopilot a level, so the levels
 * are a fixed set made with the original's own code at build time: for each
 * world, levels 1..LV_PER_WORLD (from level 9 on the original's levels are
 * all alike but for the seed: later levels go round the last two).
 */
#ifndef LV_REC_H
#define LV_REC_H

#define LV_PER_WORLD  10
#define LV_SEED(w, lv) (2026 + 10 * (w) + (lv) - 1)    /* the original's first seed is 2026 */
#define LEVEL_BANK    40                 /* banks 40.. (8K pages 80..) */
#define LEVEL_BANKS   51
#define LV_HDR_PAGES  1                  /* the records, then the heights */
#define LV_NFIELDS    51                 /* int32 values a record (gen_levels.c's order) */
#define LV_REC_SIZE   (LV_NFIELDS * 4)
#define LV_COUNT      (3 * LV_PER_WORLD)

/* the record's fields (int32 each, little-endian); a height is
   hbase + its 3 bytes (unsigned, low first) */
enum {
    LVF_WORLD, LVF_LEVEL, LVF_SEED,
    LVF_PARAMS,                          /* fuel start_alt start_h start_dist pad_scale wind */
    LVF_N = LVF_PARAMS + 6, LVF_SIZE, LVF_CELL, LVF_HALF, LVF_INV_CELL,
    LVF_PADS,                            /* 3 x (x z r y mult) */
    LVF_P = LVF_PADS + 15, LVF_V = LVF_P + 3,
    LVF_PITCH = LVF_V + 3, LVF_ROLL, LVF_WP, LVF_WR, LVF_THROTTLE,
    LVF_FUEL, LVF_FUEL_MAX, LVF_WIND_DIR, LVF_WIND_PH, LVF_PROBE_USED, LVF_PROBE_OUTCOME,
    LVF_HMAX, LVF_HBASE, LVF_RPP, LVF_ROWB, LVF_PAGE,
    LVF_END
};

#endif
