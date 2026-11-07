/*
 * screen.c - the Next's screen for Stack Tower: Layer 2 in its 320 x 256
 * mode (the original's 320 x 256 drawing, pixel for pixel) double
 * buffered in banks 16-20 and 21-25, the title painting in its own banks
 * with its own palette, and the tilemap over them as a text layer (the
 * title's words over the painting, the help pages).
 */
#include <arch/zxn.h>
#include "nextgfx.h"
#include "nexttile.h"
#include "sprtext.h"
#include "gfx_data.h"
#include "stack.h"

#define ROM_FONT ((const uint8_t *)0x3D00)
#define RGB9(r, g, b) ((uint16_t)((r) << 6 | (g) << 3 | (b)))

uint8_t scr_back;
static uint8_t front_bank = GFX_SCR_BANK;

/* a palette from the palettes' page (through MMU slot 0, then the ROM back) */
static void load_pal(uint16_t offset)
{
    ZXN_WRITE_REG(0x50, GFX_PAL_PAGE);
    pal_load(1, (const uint16_t *)offset, 256);
    ZXN_WRITE_REG(0x50, 0xFF);
}

void scr_init(void)
{
    static const uint16_t text_rgb[6] = { RGB9(7, 7, 7), RGB9(7, 7, 1), RGB9(1, 7, 2), RGB9(4, 4, 5),
                                          RGB9(1, 7, 7), RGB9(7, 1, 1) };
    uint8_t p;
    load_pal(0);
    dma_copy_banks(GFX_BACK_BANK, GFX_SCR_BANK, 5);      /* the .nex filled buffer A */
    l2w_slot = 2;                                        /* code from 0x6000: Layer 2 drawn through slot 2 */
    l2w_restore = 10;
    l2w_init(GFX_SCR_BANK);
    l2w_bank = GFX_BACK_BANK;
    scr_back = 1;

    tm_init();
    tm_font(0, ROM_FONT, 1, 1, 0);                       /* tile 0: blank, see-through */
    tm_font(1, ROM_FONT, 1, 1, 2);                       /* tile 1: blank, black */
    tm_font(32, ROM_FONT, 64, 1, 0);                     /* text over the picture: ' ' .. '_' */
    tm_font(96, ROM_FONT, 64, 1, 2);                     /* text on black (definitions end in page 10) */
    for (p = 0; p < 6; p++) {
        tm_pal(p, 1, text_rgb[p]);
        tm_pal(p, 2, 0);
    }
    ZXN_WRITE_REG(0x15, 0x13);                           /* sprites on, layers U S L: the text on top */

    spr_init();                                          /* big words: sprites (panel.c big_words) */
    sprtext_init(0, "STACKOWERPDUG!INLBQMV");
    sprtext_colour(BIG_YELLOW, text_rgb[TP_YELLOW]);
    sprtext_colour(BIG_CYAN, text_rgb[TP_CYAN]);
    sprtext_colour(BIG_RED, text_rgb[TP_RED]);
    sprtext_colour(BIG_GREEN, text_rgb[TP_GREEN]);
}

void scr_flip(void)
{
    front_bank = l2w_bank;
    ZXN_WRITE_REG(0x12, front_bank);
    l2w_bank = front_bank == GFX_SCR_BANK ? GFX_BACK_BANK : GFX_SCR_BANK;
    scr_back ^= 1;
}

void scr_show_painting(uint8_t on)
{
    if (on) {
        ZXN_WRITE_REG(0x12, GFX_TITLE_BANK);
        load_pal(512);
    } else {
        ZXN_WRITE_REG(0x12, front_bank);
        load_pal(0);
    }
}

void tm_text(uint8_t col, uint8_t row, const char *s, uint8_t pal) { tm_print(col, row, s, 32, pal); }
void tm_text_box(uint8_t col, uint8_t row, const char *s, uint8_t pal) { tm_print(col, row, s, 96, pal); }

void tm_text_centre(uint8_t row, const char *s, uint8_t pal)
{
    uint8_t n = 0;
    while (s[n]) n++;
    tm_print((uint8_t)((TM_COLS - n) / 2), row, s, 32, pal);
}

void tm_blank(void) { tm_clear(); }
void tm_cover(void) { tm_fill(0, 0, TM_COLS, TM_ROWS, 1, 0); }
