/*
 * hud.c - Boing Twist's text (the original's hud.c and draw_hud) on the
 * Next's tilemap: 40 x 32 cells over the whole display, Layer 2's room and
 * ball in the middle 32 x 24, so the bars of the play screen sit in the
 * border. The Spectrum's ROM font with a drop shadow (the original's text
 * has one), on nothing or on a dark panel (the original shades the room
 * behind its lists: the tilemap can only cover it). The title's big letters
 * are the font at three times its size.
 */
#include <string.h>
#include <arch/zxn.h>
#include "nexttile.h"
#include "hud.h"
#include "bt.h"

#define T_FONT  1                        /* tiles 1..64: characters 32..95 (capitals), on nothing */
#define T_PFONT 65                       /* 65..128: on the panel */
#define T_PANEL 129
#define T_BIG   130                      /* the title's letters, 3 x 3 tiles each (..201) */
#define RGB9(r, g, b) ((uint16_t)((r) << 6 | (g) << 3 | (b)))

#define rom_font ((const uint8_t *)0x6000)  /* main.c left the ROM font in SAVE_PAGE: mapped there by hud_init */

/* the original's colours: C_WHITE, C_YELLOW, C_RED, C_CYAN, C_DIM, green, grey-blue */
static const uint16_t ink_rgb[7] = {
    RGB9(7, 7, 7), RGB9(7, 7, 1), RGB9(7, 1, 1), RGB9(2, 6, 7), RGB9(5, 5, 5), RGB9(2, 7, 2), RGB9(4, 4, 5)
};

static void shadow_tile(uint8_t tile, const uint8_t *g, uint8_t bg)
{
    uint8_t def[32], y, x, *d = def;
    for (y = 0; y < 8; y++) {
        uint8_t row = g[y], sh = y ? g[y - 1] >> 1 : 0;
        for (x = 0; x < 8; x += 2, d++) {
            uint8_t m0 = 0x80 >> x, m1 = 0x40 >> x;
            uint8_t a = row & m0 ? 1 : sh & m0 ? 2 : bg, b = row & m1 ? 1 : sh & m1 ? 2 : bg;
            *d = a << 4 | b;
        }
    }
    tm_def(tile, def, 1);
}

void hud_init(void)
{
    uint8_t k, def[32];
    static const char big[] = "BOINGTWS";
    ZXN_WRITE_REG(0x53, SAVE_PAGE);
    tm_init();
    for (k = 0; k < 64; k++) {
        shadow_tile(T_FONT + k, rom_font + k * 8, 0);
        shadow_tile(T_PFONT + k, rom_font + k * 8, 3);
    }
    memset(def, 0x33, 32);
    tm_def(T_PANEL, def, 1);
    for (k = 0; k < 8; k++) tm_glyphnx(T_BIG + k * 9, rom_font + (big[k] - 32) * 8, 3, 1, 2, 2);
    for (k = 0; k < 7; k++) {
        tm_pal(k, 1, ink_rgb[k]);
        tm_pal(k, 2, RGB9(0, 0, 0));
        tm_pal(k, 3, RGB9(1, 1, 2));         /* the panel */
    }
    ZXN_WRITE_REG(0x53, 11);
}

uint8_t hud_on_panel;

void hud_clear(void) { tm_clear(); }

void hud_text(uint8_t col, uint8_t row, const char *s, uint8_t ink)
{
    tm_print(col, row, s, hud_on_panel ? T_PFONT : T_FONT, ink);
}

void hud_ctext(uint8_t row, const char *s, uint8_t ink)
{
    hud_text(20 - (uint8_t)(strlen(s) + 1) / 2, row, s, ink);
}

void hud_rtext(uint8_t col_end, uint8_t row, const char *s, uint8_t ink)
{
    hud_text(col_end - (uint8_t)strlen(s), row, s, ink);
}

void hud_panel(uint8_t col, uint8_t row, uint8_t w, uint8_t h)
{
    tm_fill(col, row, w, h, T_PANEL, 0);
}

void hud_blank(uint8_t col, uint8_t row, uint8_t w, uint8_t h)
{
    tm_fill(col, row, w, h, 0, 0);
}

/* the title, three cells a letter: "BOING TWIST" */
void hud_big_title(uint8_t row, uint8_t ink)
{
    static const char word[] = "BOING TWIST";
    static const char big[] = "BOINGTWS";
    uint8_t i, col = 20 - 33 / 2 - 1, r, c;
    for (i = 0; word[i]; i++, col += 3) {
        const char *p = strchr(big, word[i]);
        if (!p) continue;
        for (r = 0; r < 3; r++)
            for (c = 0; c < 3; c++) tm_put(col + c, row + r, T_BIG + (p - big) * 9 + r * 3 + c, ink);
    }
}
