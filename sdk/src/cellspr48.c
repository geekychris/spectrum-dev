/* Tile room + per-cell composed sprites on the 48K screen (see cellspr48.h). */
#include <stdint.h>
#include <string.h>
#include <arch/zx.h>
#include "cellspr48.h"

cs_sprite cs_spr[CS_MAX_SPR];
uint8_t *cs_map;
uint8_t cs_w, cs_h, cs_col, cs_row;
const uint8_t *cs_tiles;
const uint8_t *cs_attrs;
uint8_t cs_last_cells;

uint8_t *cs_map_row[24];
uint8_t cs_dirty[24 * 4];         /* one bit per cell: marked this frame (row * 4 + c / 8) */
uint8_t cs_list_c[CS_MAX_DIRTY], cs_list_r[CS_MAX_DIRTY];
uint8_t cs_list_n;
uint8_t cs_overflow;
static const uint8_t bit8[8] = {1, 2, 4, 8, 16, 32, 64, 128};


/* cellspr48_out.asm: the drawn sprites it composes over the tiles, and the
   loop that composes and writes the cells in the dirty list */
extern uint8_t cs_nact;
extern uint8_t cs_act[9 * CS_MAX_SPR];
extern void cs_flush(void);

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

/* mark the cells under a sprite drawn at (x, y), w+1 bytes (shifted) x h */
static void mark_area(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    uint8_t c0 = x >> 3, c1 = c0 + w, r0 = y >> 3, r1 = (uint8_t)(y + h - 1) >> 3, c;
    if (!(x & 7)) c1--;                       /* aligned: the extra byte is empty */
    for (; r0 <= r1; r0++)
        for (c = c0; c <= c1; c++) cs_mark(c, r0);
}

/* the drawn sprites for the composer: c0, c1, y, h, stride, gfx, keep, ink */
static void build_active(void)
{
    uint8_t i, *a = cs_act;
    cs_sprite *s = cs_spr;
    cs_nact = 0;
    for (i = 0; i < CS_MAX_SPR; i++, s++) {
        if (!s->don) continue;
        a[0] = s->dx >> 3;
        a[1] = a[0] + s->dw - ((s->dx & 7) ? 0 : 1);   /* aligned: the extra byte is empty */
        a[2] = s->dy;
        a[3] = s->dh;
        a[4] = s->dw + 1;
        *(const uint8_t **)(a + 5) = s->dgfx;
        a[7] = s->keep;
        a[8] = s->dink;
        a += 9;
        cs_nact++;
    }
}

/* the pre-shifted copy for shift x & 7 */
static const uint8_t *shifted(const cs_sprite *s)
{
    const uint8_t *p = s->gfx;
    uint16_t step = 0;
    uint8_t k = s->w + 1;
    while (k--) step += s->h;
    k = s->x & 7;
    while (k--) p += step;
    return p;
}

void cs_draw_all(void)
{
    uint8_t r, c, i;
    for (i = 0; i < CS_MAX_SPR; i++) {
        cs_sprite *s = &cs_spr[i];
        s->don = s->on;
        if (s->on) {
            s->dx = s->x; s->dy = s->y; s->dw = s->w; s->dh = s->h; s->dink = s->ink;
            s->dgfx = shifted(s);
            s->dsrc = s->gfx;
        }
    }
    build_active();
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
    uint8_t i;
    cs_sprite *s = cs_spr;
    for (i = 0; i < CS_MAX_SPR; i++, s++) {
        const uint8_t *g;
        if (!s->on) {
            if (s->don) { mark_area(s->dx, s->dy, s->dw, s->dh); s->don = 0; }
            continue;
        }
        if (s->don && s->dx == s->x && s->dy == s->y && s->dsrc == s->gfx && s->dink == s->ink) continue;
        g = shifted(s);
        if (s->don) mark_area(s->dx, s->dy, s->dw, s->dh);
        mark_area(s->x, s->y, s->w, s->h);
        s->dx = s->x; s->dy = s->y; s->dw = s->w; s->dh = s->h; s->dink = s->ink;
        s->dgfx = g;
        s->dsrc = s->gfx;
        s->don = 1;
    }
    if (cs_overflow) { cs_draw_all(); cs_last_cells = 255; return; }
    cs_last_cells = cs_list_n;
    if (cs_list_n) {
        build_active();
        cs_flush();                           /* cellspr48_out.asm: compose the list */
    }
}
