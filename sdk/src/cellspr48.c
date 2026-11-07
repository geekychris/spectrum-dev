/* Tile room + per-cell composed sprites on the 48K screen (see cellspr48.h). */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "cellspr48.h"

cs_sprite cs_spr[CS_MAX_SPR];
uint8_t cs_nspr = CS_MAX_SPR;
uint8_t *cs_map;
uint8_t cs_w, cs_h, cs_col, cs_row;
const uint8_t *cs_tiles;
const uint8_t *cs_attrs;
uint8_t cs_last_cells;

/* shared with the asm (cellspr48_*.asm) */
uint8_t *cs_map_row[24];
uint8_t cs_dirty[24 * 4];         /* one bit per cell: marked this frame (row * 4 + c / 8) */
uint8_t cs_list_c[CS_MAX_DIRTY], cs_list_r[CS_MAX_DIRTY];
uint8_t cs_list_n;
uint8_t cs_overflow;

/* cellspr48_spr.asm: marks what moved, lists the drawn sprites (cs_act: c0,
   c1, y, h, stride, gfx, keep, ink) for cellspr48_out.asm's composer */
extern uint8_t cs_nact;
uint8_t cs_act[9 * CS_MAX_SPR];       /* here, so the C and the asm agree on CS_MAX_SPR */
extern void cs_sprite_pass(void);
extern void cs_flush(void);

/* cellspr48_ref.asm */
extern const uint8_t *cs_ref_c, *cs_ref_r;
extern uint8_t cs_ref_n, cs_ref_first, cs_ref_lines, cs_nbox;
uint8_t cs_box[4 * CS_MAX_SPR];
extern void cs_refresh_loop(void);

void cs_init(uint8_t *map, uint8_t w, uint8_t h, uint8_t col, uint8_t row,
             const uint8_t *tiles, const uint8_t *attrs)
{
    uint8_t r;
    cs_map = map;
    cs_w = w;
    cs_h = h;
    cs_col = col;
    cs_row = row;
    cs_tiles = tiles;
    cs_attrs = attrs;
    for (r = 0; r < h; r++, map += w) cs_map_row[r] = map;
    memset(cs_dirty, 0, sizeof(cs_dirty));
    cs_list_n = cs_overflow = 0;
    memset(cs_spr, 0, sizeof(cs_spr));
    cs_nspr = CS_MAX_SPR;
}

void cs_set(uint8_t c, uint8_t r, uint8_t id)
{
    if (cs_map_row[r][c] == id) return;
    cs_map_row[r][c] = id;
    cs_mark(c, r);
}

void cs_mark_id(uint8_t id)
{
    uint8_t r, c;
    for (r = 0; r < cs_h; r++) {
        const uint8_t *p = cs_map_row[r];
        for (c = 0; c < cs_w; c++)
            if (p[c] == id) cs_mark(c, r);
    }
}

void cs_draw_all(void)
{
    uint8_t r, c, i;
    for (i = 0; i < cs_nspr; i++) cs_spr[i].don = 0;
    cs_sprite_pass();                         /* what is drawn now (its marks are dropped) */
    memset(cs_dirty, 0, sizeof(cs_dirty));
    cs_overflow = 0;
    for (r = 0; r < cs_h; r++) {              /* a row at a time through the asm flush */
        for (c = 0; c < cs_w; c++) {
            cs_list_c[c] = c;
            cs_list_r[c] = r;
        }
        cs_list_n = cs_w;
        cs_flush();
    }
}

void cs_update(void)
{
    cs_sprite_pass();                         /* cellspr48_spr.asm: marks what moved */
    if (cs_overflow) { cs_draw_all(); cs_last_cells = 255; return; }
    cs_last_cells = cs_list_n;
    if (cs_list_n) cs_flush();                /* cellspr48_out.asm: compose the list */
}

void cs_refresh(const uint8_t *cols, const uint8_t *rows, uint8_t n, uint8_t first, uint8_t lines)
{
    uint8_t i, *b = cs_box;
    const uint8_t *a = cs_act;
    for (i = 0; i < cs_nact; i++, a += 9, b += 4) {   /* the cells each drawn sprite covers */
        b[0] = a[0];
        b[1] = a[1];
        b[2] = a[2] >> 3;
        b[3] = (uint8_t)(a[2] + a[3] - 1) >> 3;
    }
    cs_nbox = cs_nact;
    cs_ref_c = cols;
    cs_ref_r = rows;
    cs_ref_n = n;
    cs_ref_first = first;
    cs_ref_lines = lines;
    cs_refresh_loop();
}
