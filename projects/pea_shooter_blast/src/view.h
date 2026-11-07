/* view.h - the picture (view.c), the HUD (hud.c), the sound (sound.c) */
#ifndef VIEW_H
#define VIEW_H
#include <stdint.h>
#include "psb.h"

/* modes of the port's shell around the original's states */
enum { MODE_TITLE, MODE_GAME, MODE_ENTRY };

void view_init(void);
void view_playfield(uint8_t level, int16_t px);   /* scroll to px, drawing what comes into view */
void view_sprites(const GameState *gs);
void view_sprites_off(void);
void view_title(void);
void view_blank(void);
extern uint16_t view_cols;

void hud_init(void);
void hud_frame(const GameState *gs, uint8_t mode);
extern uint8_t hud_title_page;

void snd_init(void);
void snd_music(uint8_t on);
void snd_play(uint8_t sfx);
void snd_tick(void);
uint8_t snd_reg(uint8_t chip, uint8_t reg);

#endif
