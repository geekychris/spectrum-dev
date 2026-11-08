/* hud.c: the text and the big letters on the tilemap and sprites, over the
   view (Orbit Docking's hud.c, for this game) */
#ifndef HUD_H
#define HUD_H
#include <stdint.h>

/* ink: a tilemap palette offset, and the big letters' sprite palette offset */
enum { TX_WHITE, TX_AMBER, TX_RED, TX_GREEN, TX_CYAN, TX_GREY, TX_BLACK, TX_RING1, TX_RING2, TX_RING5, TX_INKS };
/* big text groups (sprites: each letter over its shadow) */
enum { BIG_TITLE, BIG_MSG, BIG_GROUPS };

/* the ROM's characters 32..127, copied at start (main.c) for hud_init():
   into the polygons' vertex buffer, which is free until the first picture
   (resident memory is full) */
#include "vfill.h"
#define rom_font ((uint8_t *)vf_pv)
void hud_init(void);
void hud_world(uint8_t world);           /* the panel's colour (the world's pen 2) */
void hud_clear(void);                    /* text and big text */
extern uint8_t hud_on_panel;             /* 1: text goes on a panel (its colour behind) */
void hud_text(uint8_t col, uint8_t row, const char *s, uint8_t ink);
void hud_ctext(uint8_t row, const char *s, uint8_t ink);
void hud_panel(uint8_t col, uint8_t row, uint8_t w, uint8_t h);    /* the original's g_box */
void hud_strip(uint8_t row, uint8_t h);  /* a plain panel across the screen (the HUD's) */
/* big text, the original's 16 x 16 font (at twice its size if zoom), x y on
   the 320 x 256 display; "" hides the group */
void hud_big(uint8_t group, int16_t x, int16_t y, const char *s, uint8_t ink, uint8_t zoom);
void hud_big_centre(uint8_t group, int16_t y, const char *s, uint8_t ink, uint8_t zoom);
void hud_fuel(uint8_t col, uint8_t row, uint8_t k, uint8_t ink);   /* a gauge of 6 tiles, k pixels of 48 */
#endif
