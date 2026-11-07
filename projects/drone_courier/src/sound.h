/* sound.c: the original's effects (and a title tune) on the TurboSound */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
enum { SFX_PICKUP, SFX_DELIVER, SFX_BUMP, SFX_BROKEN, SFX_LATE, SFX_GUST, SFX_DAY, SFX_OVER, SFX_WIN, SFX_HUM, SFX_COUNT };
void snd_init(void);
void snd_play(uint8_t id);
void snd_music(uint8_t on);             /* 0 off, 1 the title's, 2 quiet (in play) */
void snd_tick(void);                    /* once a frame */
uint8_t snd_reg(uint8_t chip, uint8_t r);
#endif
