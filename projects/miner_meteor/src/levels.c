/*
 * The caverns: tile maps are in levels.txt (packed into levels_data.c by
 * tools/make_data.sh); the guardians and palette variants are the
 * original's levels.cpp tables (with its fixes of amiga_games #53).
 */
#include <stdint.h>
#include "game.h"
#include "levels_data.h"

static const GuardianSpec LVL_NONE[] = {
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

static const GuardianSpec LVL3_GUARDS[] = {
    /* Two horizontal patrols on the empty corridors + one on the top ledge.
       fix (as in the original, amiga_games #53): the first was at y 48,
       inside the row-3 ledge, not on it */
    { 0, 4, +2, 0, 32, 32, 32, 288 },
    { 0, 6, -2, 0, 240, 128, 32, 288 },
    { 0, 5, +2, 0, 128, 176, 96, 224 },
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

static const GuardianSpec LVL5_GUARDS[] = {
    { 0, 4, +2, 0, 48, 80, 32, 288 },      /* top level patroller */
    { 0, 6, +3, 0, 200, 176, 32, 288 },    /* mid patroller (fast) */
    { 0, 5, -2, 0, 240, 144, 32, 288 },    /* mid-lower patroller */
    { 0xFF, 0, 0, 0, 0, 0, 0, 0 }
};

const GuardianSpec *const level_guards[LV_COUNT] = {
    LVL_NONE, LVL_NONE, LVL_NONE, LVL3_GUARDS, LVL_NONE, LVL5_GUARDS
};

/* the original's palette_variant per cavern (it doesn't use them; the port
   colours each cavern's walls with it) */
const uint8_t level_palette[LV_COUNT] = { 0, 1, 2, 3, 0, 1 };

const uint8_t num_levels = LV_COUNT;
