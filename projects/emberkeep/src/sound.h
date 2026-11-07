/* Emberkeep - sound on the Next's TurboSound (chip 0) */
#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

#define SND_RUNE  1
#define SND_OIL   2
#define SND_DEATH 3
#define SND_EXIT  4
#define SND_LOW   5     /* the lantern is running low */
#define SND_OVER  6
#define SND_WIN   7

#define MUSIC_OFF   0
#define MUSIC_TITLE 1
#define MUSIC_DEEP  2   /* in the chambers: a slow low line */

void snd_init(void);
void snd_play(uint8_t s);
void snd_music(uint8_t m);
void snd_tick(void);    /* once per 50 Hz frame */

#endif
