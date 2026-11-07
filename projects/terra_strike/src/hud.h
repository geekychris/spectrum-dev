/* Terra Strike - the HUD on the tilemap (hud.c) */
#ifndef HUD_H
#define HUD_H

#include <stdint.h>
#include "ts.h"

void hud_init(void);                    /* fonts from the ROM: before it is paged out */
void hud_reset(void);                   /* the panel, everything redrawn */
void hud_draw(const World *w, uint8_t paused);
void hud_title(uint8_t frame);

#endif
