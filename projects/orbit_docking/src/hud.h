/* hud.c: the instruments and text on the tilemap and sprites */
#ifndef HUD_H
#define HUD_H
#include <stdint.h>
#include "fix16.h"

/* ink: a tilemap palette offset, and the big letters' sprite palette offset */
enum { TX_WHITE, TX_YELLOW, TX_RED, TX_GREEN, TX_CYAN, TX_GREY, TX_BLACK };
/* big text groups: sprites (ink, its shadow under it) */
enum { BIG_TITLE1, BIG_TITLE2, BIG_BANNER, BIG_GROUPS };

extern uint8_t rom_font[768];            /* the ROM's characters 32..127, copied at start */
void hud_init(void);
void hud_clear(void);                    /* text, big text and instruments */
extern uint8_t hud_on_panel;             /* 1: text goes on a panel (its colour behind) */
void hud_text(uint8_t col, uint8_t row, const char *s, uint8_t ink);
void hud_ctext(uint8_t row, const char *s, uint8_t ink);
void hud_panel(uint8_t col, uint8_t row, uint8_t w, uint8_t h);    /* the original's g_box */
/* big text, the original's 16 x 16 font (at twice its size if zoom), x y on
   the 320 x 256 display; "" hides the group */
void hud_big(uint8_t group, int16_t x, int16_t y, const char *s, uint8_t ink, uint8_t zoom);
void hud_big_centre(uint8_t group, int16_t y, const char *s, uint8_t ink, uint8_t zoom);
/* the axis box (the craft at x, y), the lives, the roll dial */
void hud_instruments(fix x, fix y, int8_t lives, int16_t roll_err, uint8_t roll_ok);
void hud_instruments_hide(void);
void hud_dial(int16_t err, uint8_t ok);
void hud_fuel(uint8_t k, uint8_t ink);   /* k pixels of 78 */
void hud_clip(uint8_t ybot);             /* sprites shown on lines 0..ybot only */
#endif
