/* Orbital Patrol's sound on the Next: the original's MOD on TurboSound
   chips 1 and 2 (src/music.c, tools/make_music.sh), its effects on chip 0. */
#ifndef SOUND_H
#define SOUND_H
#include <stdint.h>
#include "game.h"

void sound_init(void);
void sound_events(GameState *gs);     /* after each tick: the ev_* flags -> effects, cleared */
void sound_frame(void);               /* from the frame interrupt */
#endif
