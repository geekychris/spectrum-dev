/*
 * title.c - the title's picture in Layer 2 (draw.c's draw_title, the parts
 * that don't change: stars, the big words, the tank). In page 88, mapped at
 * 0x4000 while it runs (view.c's view_title); it draws through l2w_*,
 * which borrow 0x6000.
 */
#include <stdint.h>
#include <string.h>
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nextsprl.h"
#include "l2rle.h"
#include "psb.h"
#include "gfx_data.h"
#include "view.h"

static void fill(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t c)    /* RectFill */
{
    l2w_fill(x0, (uint8_t)y0, x1 - x0 + 1, y1 - y0 + 1, c);
}

/* draw.c's draw_string at a scale: each pixel of the 5x7 font a
   scale x scale square */
extern const uint8_t font_5x7[96][7];
static void l2_string(int16_t x, int16_t y, const char *s, uint8_t sc, uint8_t c)
{
    for (; *s; s++, x += 6 * sc) {
        uint8_t g[7], r, col;
        memcpy(g, font_5x7[(uint8_t)*s - 32], 7);   /* the font is in page 84, which the fills map over */
        for (r = 0; r < 7; r++)
            for (col = 0; col < 5; col++)
                if (g[r] & (0x10 >> col)) l2w_fill(x + col * sc, (uint8_t)(y + r * sc), sc, sc, c);
    }
}

static void l2_centre(int16_t y, const char *s, uint8_t sc, uint8_t c)
{
    l2_string(SCREEN_W / 2 - ((int16_t)strlen(s) * 6 * sc - sc) / 2, y, s, sc, c);
}

/* draw.c's draw_title, the parts that don't change: the starfield, the
   title and the tank (the rest is the tilemap's) */
void title_draw(void)
{
    uint32_t seed = 42;
    uint8_t i;
    int16_t cx = SCREEN_W / 2;
    for (i = 0; i < 40; i++) {
        seed = seed * 1103515245UL + 12345UL;
        l2w_plot((int16_t)((seed >> 16) % SCREEN_W), (uint8_t)((seed >> 8) % SCREEN_H), 1);
    }
    fill(cx - 20, 120, cx + 10, 134, 2);
    fill(cx - 14, 116, cx + 2, 122, 3);
    fill(cx + 2, 118, cx + 18, 120, 3);
    fill(cx - 22, 135, cx + 12, 140, 13);
    l2_centre(30, "PEA SHOOTER", 3, 8);
    l2_centre(60, "BLAST", 4, 10);
}

/* an empty screen (the high-score entry) */
