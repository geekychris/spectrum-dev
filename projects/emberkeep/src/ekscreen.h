/* Emberkeep - the Next's screen (screen.c) */
#ifndef EKSCREEN_H
#define EKSCREEN_H

#include <stdint.h>

#define L2_BANK 9                       /* Layer 2 320 x 256: banks 9-13 */

/* tilemap palettes */
#define TP_WHITE   0
#define TP_YELLOW  1
#define TP_GREEN   2
#define TP_RED     3
#define TP_ORANGE  4
#define TP_GREY    5
#define TP_CYAN    6
#define TP_BLUE    7
#define TP_BAR     8                    /* the lantern bar */
#define TP_BAR_LOW 9                    /* ... below a quarter */
#define TP_COUNT   10

void scr_init(void);
void scr_l2_clear(void);
void scr_tile(uint8_t col, uint8_t row, uint8_t tile);   /* a 16 x 16 tile at a grid cell */
void scr_cell(uint8_t col, uint8_t row);                 /* a grid cell as it is now */
void scr_room(void);                                     /* the whole chamber */
void scr_portal(void);                                   /* the open portal's flicker */
void scr_sprites(uint8_t show);
void scr_lantern(uint8_t on);                            /* the light: clip windows by the copper */
void scr_lantern_reset(void);
void scr_hud(uint8_t force);

void tm_text(uint8_t col, uint8_t row, const char *s, uint8_t pal);
void tm_centre(uint8_t row, const char *s, uint8_t pal);
void tm_num(uint8_t col, uint8_t row, uint32_t v, uint8_t digits, uint8_t pal);

#endif
