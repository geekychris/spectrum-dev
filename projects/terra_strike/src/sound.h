/* Terra Strike - music and effects on the Next's TurboSound (sound.c) */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

void snd_init(void);
void snd_event(int16_t ev);             /* an EV_* from the simulation */
void snd_music(uint8_t on);
void snd_tick(uint8_t frames);

#endif
