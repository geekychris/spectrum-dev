/* sound.c - TurboSound: the effects on chip 2, the tune on chips 0 and 1 */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>

enum { SFX_THUD, SFX_CLICK, SFX_FLIP, SFX_SOLVED, SFX_KEY, SFX_COUNT };

void snd_init(void);
void snd_music(uint8_t on);
void snd_play(uint8_t sfx);
void snd_tick(void);                     /* each frame */
uint8_t snd_reg(uint8_t chip, uint8_t reg);
#endif
