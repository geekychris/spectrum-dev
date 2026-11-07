/* hud.c: Drone Courier's text, panels and gauges on the tilemap and sprites */
#ifndef HUD_H
#define HUD_H
#include <stdint.h>

/* inks: a tilemap palette offset and a sprite palette offset each (the
   original's pens 1 white, 27 yellow, 26 red, 28 green, 29 cyan, 3 grey,
   0 black, 14 the depot's blue, 15 the customer's green) */
enum { TX_WHITE, TX_YELLOW, TX_RED, TX_GREEN, TX_CYAN, TX_GREY, TX_BLACK, TX_DEPOT, TX_CUST, TX_INKS };
/* big text groups (sprites) */
enum { BIG_TITLE1, BIG_TITLE2, BIG_CLOCK, BIG_BANNER, BIG_GROUPS };

void hud_init(void);                     /* with the ROM at 0x0000 (its font) */
void hud_clear(void);                    /* every cell blank, the big text and gauges hidden */
extern uint8_t hud_on_panel;             /* 1: text goes on a panel (its colour behind) */
void hud_text(uint8_t col, uint8_t row, const char *s, uint8_t ink);
void hud_ctext(uint8_t row, const char *s, uint8_t ink);
void hud_blank(uint8_t col, uint8_t row, uint8_t n);
void hud_panel(uint8_t col, uint8_t row, uint8_t w, uint8_t h);    /* the original's g_box */
void hud_fill_panel(void);               /* the whole screen a panel (help pages) */
/* big text, the original's g_text_big in the engine's 16 x 16 font: scale 2
   (16 pixels a letter) or 4 (32, sprites at twice their size); x y on the
   320 x 256 display; "" hides the group */
void hud_big(uint8_t group, int16_t x, int16_t y, const char *s, uint8_t ink, uint8_t scale);
void hud_big_centre(uint8_t group, int16_t y, const char *s, uint8_t ink, uint8_t scale);
void hud_arrow(uint16_t rel, uint8_t ink);   /* the arrow to the next pad, rel a 16-bit angle */
void hud_arrow_hide(void);
void hud_wind(int16_t wx, int16_t wz);   /* the wind gauge: the original's lines, pixels */
void hud_wind_hide(void);
void hud_battery(uint8_t w);
void hud_drone(void);                    /* view_drone's sprites (right after the flip) */
void hud_drone_hide(void);             /* the bar: w pixels of 80 */
#endif
