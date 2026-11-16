/* sound.h - Dot Chase's sounds (sound.c), the original's sound.h */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void snd_seed(void);
void snd_eat_spider(void);
void snd_die(void);
void snd_bonus(void);
void snd_extra_life(void);
void snd_power(void);
void snd_power_off(void);
void snd_set_pulse(uint8_t level);
void snd_pulse_off(void);
void snd_stop(void);
void snd_tick(void);                 /* each 50 Hz logic tick */
void snd_frame(void);                /* each drawn frame (plays the beeper) */
#endif
