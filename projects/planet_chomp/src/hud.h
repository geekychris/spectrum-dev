/* the HUD and the screens on the tilemap (hud.c, bank 21) */
#ifndef HUD_H
#define HUD_H
#include <stdint.h>
enum { HUD_PLAY, HUD_TITLE, HUD_OVER, HUD_ENTRY };
extern uint8_t hud_screen;
void hud_init(void);                /* the tiles and palettes (the ROM font at 0x7000 first) */
void hud_update(void);              /* each frame: what changed */
void hud_entry(const char *name, uint8_t pos, uint8_t place);   /* the initials */
#endif
