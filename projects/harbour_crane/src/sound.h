/* Music and effects on TurboSound (sound.c). */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
#include "sound_data.h"
void snd_init(void);
void snd_music(uint8_t on);          /* the title tune (chips 0 and 1), looping */
void snd_play(uint8_t sfx);          /* SFX_* on chip 2 (a new one replaces the last) */
void snd_tick(void);                 /* once per frame that passed */
uint8_t snd_reg(uint8_t chip, uint8_t reg);   /* what a chip plays now (for the test) */
#endif
