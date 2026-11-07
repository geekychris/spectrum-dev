/* made by tools/gen_music.py: do not edit */
#ifndef MUSIC_DATA_H
#define MUSIC_DATA_H
#include <stdint.h>
#define MUSIC_PATTERN 64      /* rows a pattern */
#define MUSIC_SONG 8          /* patterns in the song */
#define MUSIC_SPEED 3         /* ticks a row */
extern const uint16_t music_period[35];
extern const uint8_t music_melody[256];   /* [pattern][row]: note, 0 = rest */
extern const uint8_t music_bass[256];
extern const uint8_t music_order[8];
#endif
