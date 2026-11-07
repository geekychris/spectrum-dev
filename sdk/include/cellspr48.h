/*
 * cellspr48.h - a tile room with sprites on the 48K screen, drawn per
 * character cell: every cell a sprite left or entered (and every cell the
 * game changed) is composed once - its tile, then each sprite over it ORed
 * in - and written to the screen in one go. Nothing is erased and redrawn,
 * so sprites don't flicker and pass over tiles without damaging them.
 * Colour is per cell: a sprite sets the ink of the cells it covers, like
 * the Spectrum's single-screen platformers (later sprites win).
 *
 *   cs_init(map, w, h, col, row, tiles, attrs)
 *          map[w*h] tile ids (yours: change it with cs_set), drawn with its
 *          top-left at screen cell (col, row); tiles = 8 bytes per id,
 *          attrs = one attribute per id (both may be changed later:
 *          cs_mark_id redraws the cells using an id)
 *   cs_draw_all()          redraw the whole room (and the sprites)
 *   cs_set(c, r, id)       change a cell's tile (redrawn at the next update)
 *   cs_mark(c, r)          redraw a cell at the next update
 *   cs_mark_id(id)         redraw every cell with this tile id (animated tiles)
 *   cs_spr[i]              CS_MAX_SPR sprites: set x, y (pixels in the room),
 *                          gfx (spr_preshift() output), w (bytes), h (lines),
 *                          ink/keep (cell attr = (tile attr & keep) | ink),
 *                          on; the module sees what changed
 *   cs_update()            compose and write the cells that changed
 *
 * Sprites are any size; the room is up to 32 x 24 cells. A frame's cost is
 * about 600 T-states a dirty cell plus 100 per sprite line drawn.
 */
#ifndef ZX_CELLSPR48_H
#define ZX_CELLSPR48_H

#include <stdint.h>

#define CS_MAX_SPR  8
#define CS_MAX_DIRTY 200        /* more marks than this in one frame: whole room */

typedef struct {
    uint8_t x, y;               /* pixel position in the room */
    const uint8_t *gfx;         /* pre-shifted (spr_preshift: 8 x (w+1) x h bytes) */
    uint8_t w, h;               /* width in bytes (unshifted), lines */
    uint8_t ink, keep;          /* cell attribute = (tile attr & keep) | ink */
    uint8_t on;
    /* drawn state (the module's) */
    uint8_t dx, dy, dw, dh, don, dink;
    const uint8_t *dgfx, *dsrc;             /* the shifted copy drawn, and its gfx */
} cs_sprite;

extern cs_sprite cs_spr[CS_MAX_SPR];
extern uint8_t *cs_map;
extern uint8_t cs_w, cs_h, cs_col, cs_row;
extern const uint8_t *cs_tiles;
extern const uint8_t *cs_attrs;

extern void cs_init(uint8_t *map, uint8_t w, uint8_t h, uint8_t col, uint8_t row,
                    const uint8_t *tiles, const uint8_t *attrs);
extern void cs_draw_all(void);
extern void cs_set(uint8_t c, uint8_t r, uint8_t id);
/* cs_mark(c, r): asm (cellspr48_out.asm), the column and row in one register */
extern void cs_mark_rc(uint16_t rc) __z88dk_fastcall;
#define cs_mark(c, r) cs_mark_rc(((uint16_t)(r) << 8) | (uint8_t)(c))
extern void cs_mark_id(uint8_t id);
extern void cs_update(void);
extern uint8_t cs_last_cells;   /* cells written by the last cs_update (profiling) */

#endif
