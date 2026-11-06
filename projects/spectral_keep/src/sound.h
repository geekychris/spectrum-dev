/* the Next side of the sound (sound.c); the game calls snd_play etc. (keep.h) */
#ifndef SOUND_H
#define SOUND_H
void snd_init(void);
void snd_tick(void);                    /* once a logic step, from the main loop */
#endif
