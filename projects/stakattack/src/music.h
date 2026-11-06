/* the original's MOD, converted for TurboSound by tools/mod2ay.py */
#ifndef MUSIC_H
#define MUSIC_H
#include <stdint.h>
#define MUSIC_ROWS 768
extern const uint16_t music_lead_period[], music_bass_period[], music_ch3_period[];
extern const uint8_t music_speed;
extern const uint8_t music_rows[MUSIC_ROWS][4];
#endif
