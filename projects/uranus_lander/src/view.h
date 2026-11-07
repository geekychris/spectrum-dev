/* Uranus Lander: the Spectrum renderer (view.c). */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
extern uint8_t view_autopilot;
void view_init(void);
void view_title(void);
void view_title_frame(void);
void view_play_screen(void);
void view_frame(void);
void view_landed(void);
void view_crash(void);
void view_gameover(void);
void view_overlay_clear(void);
void view_blink(uint8_t row, uint8_t col, uint8_t w, uint8_t attr);
uint16_t view_draws(void);
#endif
