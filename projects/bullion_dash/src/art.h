/* made by tools/make_art.py */
#ifndef ART_H
#define ART_H
#include <stdint.h>
extern const uint8_t tile_gfx[], edit_gfx[], frame_gfx[];
#define FR_STAND 0
#define FR_RUN1 1
#define FR_RUN2 2
#define FR_CLIMB1 3
#define FR_CLIMB2 4
#define FR_HANG 5
#define FR_DIG 6
#define FR_FALL 7
#define FR_TRAPPED 8
#define FR_CURSOR 9
#define NUM_FRAMES 10
#endif
