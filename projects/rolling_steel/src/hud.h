#ifndef HUD_H
#define HUD_H
#include <stdint.h>
extern uint8_t rom_font[768];
extern uint32_t hud_score;
extern uint8_t hud_entry, hud_initials_pos;
extern uint8_t play_mode;                /* port: 1 play mode (the default), 0 exact (main.c) */
extern char hud_initials[4];
extern uint8_t hud_dark;                 /* the shade wanted: Layer 2's second palette at hud_dark / 8 */
void hud_init(void);
void hud_frame(void);
#endif
