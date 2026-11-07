/* sound.c: the port's tune and effects on TurboSound */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
enum { SFX_STEP, SFX_PUSH, SFX_BUMP, SFX_JEWEL, SFX_WIN, SFX_TIMEOUT, SFX_KEY, SFX_COUNT };
enum { MUS_NONE, MUS_TITLE, MUS_PLAY };
void snd_init(void);
void snd_music(uint8_t m);
void snd_play(uint8_t sfx);
void snd_tick(void);                     /* once a frame */
uint8_t snd_reg(uint8_t chip, uint8_t r);
#endif
