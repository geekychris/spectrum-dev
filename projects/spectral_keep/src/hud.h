/* The HUD and the title / end screens on the tilemap (hud.c). */
#ifndef HUD_H
#define HUD_H
#include <stdint.h>
void hud_init(void);
uint8_t hud_update(void);               /* redraws when what it shows changed; 1 if it did */
void hud_reset(void);                   /* redraw on the next update */
#endif
