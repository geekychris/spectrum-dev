/* cs_scroll: character-strip scrolling of a cellspr48 room (see cellspr48.h);
   the rows are cellspr48_shift.asm's. */
#include <stdint.h>
#include "cellspr48.h"
#include "zxgame.h"


/* the cells each drawn sprite covers, dc cells along too (where a shifted row
   moved its pixels), are marked; the drawn state is dropped so cs_update
   draws the sprites anew */
void cs_wipe_sprites(int8_t dc)
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

/* cellspr48_shift.asm: the rows */
extern void cs_scroll_rows(const uint8_t *fill, uint8_t n, uint8_t left);

void cs_scroll(int8_t dc, const uint8_t *fill)
{
    uint8_t n = dc < 0 ? -dc : dc;
    if (!n || n >= cs_w) return;
    cs_wipe_sprites(dc);
    cs_scroll_rows(fill, n, dc > 0);
}
