/*
 * state.c - the puzzle and the best scores, in the logic bank (bt.h): only
 * touched while it is mapped (never while Layer 2's write window is on).
 */
#include "bt_core.h"
#include "gfx_data.h"

BtPuzzle P;
uint16_t best_mv[NPICS][4], best_s[NPICS][4];
