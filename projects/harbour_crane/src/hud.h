/* hud.c: text on the tilemap, big letters, the LIST gauge and the wind */
#ifndef HUD_H
#define HUD_H
#include <stdint.h>
#include "hc.h"

/* ink: a tilemap palette offset (the original's pens) */
enum { TX_WHITE, TX_YELLOW, TX_RED, TX_GREEN, TX_CYAN, TX_GREY, TX_CRANE, TX_COUNT };

extern uint8_t rom_font[768];            /* the ROM's characters 32..127, copied at start */
void hud_init(void);
void hud_clear(void);
extern uint8_t hud_on_panel;             /* 1: text goes on a panel (its colour behind) */
void hud_text(uint8_t col, uint8_t row, const char *s, uint8_t ink);
void hud_ctext(uint8_t row, const char *s, uint8_t ink);
void hud_panel(uint8_t col, uint8_t row, uint8_t w, uint8_t h, uint8_t ink);   /* the original's g_box */
void hud_big_begin(void);                /* before a screen's big words */
void hud_big(uint8_t row, const char *s, uint8_t ink);   /* centred, 2 x 2 tiles a letter */
void hud_gauge_bar(void);
void hud_gauge(fix heel, fix list, int16_t wind);
void hud_gauge_hide(void);
#endif
