/* the Next side of the sound (sound.c); the game calls snd_play etc. (keep.h) */
#ifndef SOUND_H
#define SOUND_H
void snd_init(void);
void snd_tick(void);                    /* once a logic step, from the main loop */
uint8_t snd_reg(uint8_t chip, uint8_t reg);   /* read back an AY register */
#endif
