/* view.c: the play screen on the Next (Layer 2 house, copper split,
   tilemap text, sprites) */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#include "game.h"

/* tilemap pens (palette offsets): ink on background */
enum { TX_WHITE, TX_YELLOW, TX_BUBBLE, TX_RJ, TX_RED, TX_YEL2, TX_LTGREEN, TX_GREY, TX_DKBROWN, TX_PINK,
       TX_LTBLUE, TX_ORANGE, TX_GREEN, TX_HUDWHITE, TX_HUDYELLOW, TX_BANNER, TX_COUNT };
#define TX_HAT TX_RED
#define T_FONT  1                        /* tiles 1..96: characters 32..127, transparent behind */
#define T_FONTB 97                       /* 97..192: the same with pen 3 behind */
#define T_HAT   193
#define T_SOLID 194                      /* pen 3 */

/* text slots in the house */
enum { WT_NAME0 = 0, WT_BUBBLE0 = MAX_GUESTS, WT_RJ = 2 * MAX_GUESTS, WT_MSG, WT_EGG, WT_COUNT };

extern uint8_t rom_font[768];           /* the ROM's characters, copied at start-up */
void view_text_init(void);
void view_init(void);
void view_house(void);                   /* the play screen: the HUD bar, the house from the next view_camera */
void view_camera(int16_t cam);           /* scroll to the camera (copies columns, the copper split) */
void view_title(void);                   /* the title picture on Layer 2 */
void view_blank(void);                   /* Layer 2 all COL_BG (the text screens) */
void view_print(uint8_t col, uint8_t row, const char *s, uint8_t pen, uint8_t opaque);
void view_cprint(uint8_t row, const char *s, uint8_t pen, uint8_t opaque);
void view_wtext(uint8_t slot, int16_t wx, int16_t y, const char *s, uint8_t pen, uint8_t opaque);
void view_wtext_clear(void);
void view_sprites(const GameState *gs);
void view_hide_sprites(void);
#endif
