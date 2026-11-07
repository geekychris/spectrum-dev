/* Lunar Rider: TurboSound music and effects (sound.c). */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void snd_init(void);
void snd_tick(void);                    /* once per logic frame */
void snd_music(uint8_t on);
void snd_shoot(void);
void snd_explode(void);
void snd_jump(void);
void snd_checkpoint(void);
void snd_death(void);
uint8_t snd_reg(uint8_t chip, uint8_t r);   /* read an AY register (tests) */
#endif
