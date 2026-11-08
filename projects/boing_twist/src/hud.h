/* hud.c - text on the tilemap (40 x 32 cells; Layer 2 is cells 4..35 x 4..27) */
#ifndef HUD_H
#define HUD_H
#include <stdint.h>

enum { TX_WHITE, TX_YELLOW, TX_RED, TX_CYAN, TX_DIM, TX_GREEN, TX_GREY };

extern uint8_t hud_on_panel;             /* 1: text on the panel colour */
void hud_init(void);
void hud_clear(void);
void hud_text(uint8_t col, uint8_t row, const char *s, uint8_t ink);
void hud_ctext(uint8_t row, const char *s, uint8_t ink);
void hud_rtext(uint8_t col_end, uint8_t row, const char *s, uint8_t ink);
void hud_panel(uint8_t col, uint8_t row, uint8_t w, uint8_t h);
void hud_blank(uint8_t col, uint8_t row, uint8_t w, uint8_t h);
void hud_big_title(uint8_t row, uint8_t ink);
#endif
