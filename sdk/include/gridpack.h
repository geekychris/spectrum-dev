/*
 * gridpack.h - many tile grids (levels, rooms) of the same size, packed
 * together on the host by sdk/tools/gridpack.py: each tile is coded by a
 * binary arithmetic coder with fixed probabilities chosen by its neighbours
 * (the tile before it and the tile above), the model shared by all grids.
 * For level sets too big for zxrle: Bullion Dash's 150 levels of 28 x 16 take
 * ~8 KB (zxrle: ~20 KB).
 *
 *   gp_unpack(&set, i, out)   grid i (0-based) into out[w * h], row-major;
 *                             about 0.1 s for 28 x 16
 *
 * Tile ids are < 16 and a grid packs into less than 256 bytes. The set is made by gridpack.py, whose `unpack` is the
 * reference decoder (same steps).
 */
#ifndef ZX_GRIDPACK_H
#define ZX_GRIDPACK_H

#include <stdint.h>

typedef struct {
    uint8_t w, h, ntiles;           /* grid size; tile ids 0..ntiles-1 */
    const uint8_t *order;           /* tile ids by frequency (literal order) */
    const uint8_t *pa;              /* [prev][above]: P(not the tile before) in 1/256 */
    const uint8_t *pb;              /* [prev][above]: P(not the tile above) */
    const uint8_t *pl;              /* [above][k]: P(not order[k]) */
    const uint8_t *grid_len;        /* each grid's bytes in bits[] (< 256) */
    const uint8_t *bits;            /* the coded grids, MSB first */
} gp_set;

extern void gp_unpack(const gp_set *set, uint16_t index, uint8_t *out);

#endif
