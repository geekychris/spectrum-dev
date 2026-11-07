/* front.h - the shell (front.c) */
#ifndef FRONT_H
#define FRONT_H
#include <stdint.h>

void front_ui_init(const uint8_t *font);   /* the tilemap, from the ROM's font (a copy) */
void front_sprites_init(void);
void front_init(void);
void front_input(void);                     /* once a frame */
void front_tick(void);                      /* each of the original's 25 ticks a second, rules mapped */
void front_sound(void);                     /* once a frame */
void front_screen(void);                    /* the tilemap's text and bars */
void front_radar(void);                     /* the radar's blips */
void front_redraw(void);
extern uint8_t front_entering;

#endif
