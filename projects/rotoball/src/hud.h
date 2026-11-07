/* The HUD, title and full time on the tilemap (hud.c). */
#ifndef HUD_H
#define HUD_H
#include <stdint.h>
void hud_init(void);
void hud_update(uint8_t sel);   /* sel: the title's choice, 1 or 2 players */
#endif
