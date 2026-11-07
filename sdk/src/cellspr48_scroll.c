/* cs_scroll: character-strip scrolling of a cellspr48 room (see cellspr48.h). */
#include <stdint.h>
#include <string.h>
#include "cellspr48.h"
#include "zxgame.h"

extern uint8_t *cs_map_row[24];

/* the cells a drawn sprite covers, dc cells along too (where a shifted row
   moved its pixels); its drawn state is dropped so cs_update draws it anew */
static void wipe_sprites(int8_t dc)
{
    uint8_t i, r, r1, c0, c1;
    int8_t c;
    cs_sprite *s = cs_spr;
    for (i = 0; i < cs_nspr; i++, s++) {
        if (!s->don) continue;
        s->don = 0;
        c0 = s->dx >> 3;
        c1 = c0 + s->dw - ((s->dx & 7) == 0);
        r1 = (uint8_t)(s->dy + s->dh - 1) >> 3;
        for (r = s->dy >> 3; r <= r1; r++)
            for (c = (int8_t)c0; c <= (int8_t)c1; c++) {
                cs_mark(c, r);
                if ((int8_t)(c - dc) >= 0) cs_mark(c - dc, r);
            }
    }
}

void cs_scroll(int8_t dc, const uint8_t *fill)
{
    uint8_t n = dc < 0 ? -dc : dc;
    uint8_t keep = cs_w - n;            /* cells that stay on the screen, per row */
    uint8_t r, k, y, edge = dc > 0 ? keep : 0;
    uint8_t old[32];

    if (!n || n >= cs_w) return;
    wipe_sprites(dc);
    for (r = 0; r < cs_h; r++, fill += n) {
        uint8_t *row = cs_map_row[r], first = row[0], uniform = 1;
        for (k = 1; k < cs_w; k++)
            if (row[k] != first) { uniform = 0; break; }
        memcpy(old, row + edge, n);     /* what the screen shows at the edge */
        if (dc > 0) memmove(row, row + n, keep);
        else memmove(row + n, row, keep);
        memcpy(row + edge, fill, n);
        if (!uniform) {                 /* move the pixels and the colours */
            uint8_t line = (cs_row + r) << 3;
            uint8_t *a = (uint8_t *)0x5800 + ((uint16_t)(cs_row + r) << 5) + cs_col;
            for (y = 0; y < 8; y++, line++) {
                uint8_t *p = zx_row[line] + cs_col;
                if (dc > 0) memmove(p, p + n, keep);
                else memmove(p + n, p, keep);
            }
            if (dc > 0) memmove(a, a + n, keep);
            else memmove(a + n, a, keep);
        }
        for (k = 0; k < n; k++)
            if (fill[k] != old[k]) cs_mark(edge + k, r);
    }
}
