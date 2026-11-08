/* sound.c: the original's music (its MOD) and effects on TurboSound */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
enum { SFX_THRUST, SFX_RCS, SFX_LAND, SFX_CRASH, SFX_FUEL, SFX_BEEP, SFX_KEY, SFX_COUNT };
#define MUSIC_BANK 26                    /* the song's rows (tools/make_music.py) */
void snd_init(void);
void snd_music(uint8_t on);              /* the title's music */
void snd_play(uint8_t sfx);
void snd_tick(void);                     /* once a frame */
uint8_t snd_reg(uint8_t chip, uint8_t r);
#endif
