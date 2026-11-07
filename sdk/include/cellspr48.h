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
 *   cs_wipe_sprites(dc)    mark the cells the drawn sprites cover (and those dc
 *                          columns to their left) and forget them: the next
 *                          cs_update draws them again (cellspr48_scroll.c)
 *   cs_put_tile(c, r, id)  set a cell and draw its tile straight to the screen,
 *                          no composing (~250 T-states, cs_update's cells ~600):
 *                          for many cells at once (a view that jumps), after
 *                          cs_wipe_sprites(0) (cellspr48_tiles.asm)
 *   cs_set(c, r, id)       change a cell's tile (redrawn at the next update)
 *   cs_mark(c, r)          redraw a cell at the next update
 *   cs_mark_id(id)         redraw every cell with this tile id (animated tiles)
 *   cs_refresh(cols, rows, n, first, lines)
 *                          lines first..first+lines-1 of n cells' tiles straight
 *                          to the screen (asm, ~150 T-states a cell): animated
 *                          tiles that change a few lines, like conveyor belts.
 *                          Cells a sprite covers are marked instead
 *   cs_spr[i]              CS_MAX_SPR sprites: set x, y (pixels in the room),
 *                          gfx (spr_preshift() output), w (bytes), h (lines),
 *                          ink/keep (cell attr = (tile attr & keep) | ink),
 *                          on; the module sees what changed
 *   cs_nspr                how many of cs_spr[] the game uses (CS_MAX_SPR after
 *                          cs_init; fewer is faster)
 *   cs_update()            compose and write the cells that changed
 *
 *   cs_scroll(dc, fill)    scroll the room dc cells left (dc > 0: the view moves
 *                          right) or right (dc < 0), as character-strip scrolling:
 *                          the map shifts, fill[] (|dc| ids a row, row by row)
 *                          comes in at the edge. On the screen, a cell that shows
 *                          the same tile as the one dc along (sky) is already
 *                          right; a row with up to 8 others gets them redrawn,
 *                          a row with more has its pixels and colours moved.
 *                          Drawn sprites are wiped and drawn again at the next
 *                          cs_update, where the game puts them (in the scrolled
 *                          room's pixels). Call it after cs_update (with no cells
 *                          marked). cellspr48_scroll.c, cellspr48_shift.asm
 *   cs_scroll_same         optional (0 = none): cs_h flags, nonzero = the game
 *                          knows this row equals itself shifted (sky), so
 *                          cs_scroll skips comparing it
 *
 * Sprites are any size; the room is up to 32 x 24 cells. A frame's cost is
 * about 600 T-states a dirty cell plus 100 per sprite line drawn.
 */
#ifndef ZX_CELLSPR48_H
#define ZX_CELLSPR48_H

#include <stdint.h>

#define CS_MAX_SPR  16            /* cellspr48_out.asm / _ref.asm size their tables by it */
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
extern uint8_t cs_nspr;
extern uint8_t *cs_map;
extern uint8_t cs_w, cs_h, cs_col, cs_row;
extern const uint8_t *cs_tiles;
extern const uint8_t *cs_attrs;

extern void cs_init(uint8_t *map, uint8_t w, uint8_t h, uint8_t col, uint8_t row,
                    const uint8_t *tiles, const uint8_t *attrs);
extern void cs_draw_all(void);
extern void cs_wipe_sprites(int8_t dc);
extern void cs_put_tile(uint8_t c, uint8_t r, uint8_t id);
extern void cs_set(uint8_t c, uint8_t r, uint8_t id);
/* cs_mark(c, r): asm (cellspr48_out.asm), the column and row in one register */
extern void cs_mark_rc(uint16_t rc) __z88dk_fastcall;
#define cs_mark(c, r) cs_mark_rc(((uint16_t)(r) << 8) | (uint8_t)(c))
extern void cs_mark_id(uint8_t id);
extern void cs_update(void);
extern void cs_scroll(int8_t dc, const uint8_t *fill);
extern const uint8_t *cs_scroll_same;
extern void cs_refresh(const uint8_t *cols, const uint8_t *rows, uint8_t n, uint8_t first, uint8_t lines);
extern uint8_t cs_last_cells;   /* cells written by the last cs_update (profiling) */
extern uint8_t cs_list_n;       /* cells marked for the next cs_update */

#endif
