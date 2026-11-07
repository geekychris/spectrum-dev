/* sound.c (game.h has the effects' calls) */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
void snd_init(void);
void snd_title(uint8_t on);         /* port: the title tune */
void snd_tick(void);                /* once per frame that passed */
uint8_t snd_reg(uint8_t c, uint8_t r);   /* an AY register of chip c */
#endif
