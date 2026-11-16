/* sound.c: the music (the original's MODs through mod2ay) and the effects */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void snd_init(void);
void snd_tick(void);                     /* once a frame */
void music_for_state(int16_t state);
uint8_t snd_reg(uint8_t chip, uint8_t r);  /* an AY register read back */     /* the original's: party.mod on the title and in play, birthday.mod after */
#endif
