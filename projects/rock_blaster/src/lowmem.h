/*
 * Rock Blaster: the RAM below the program (0x5D00-0x7FFF). The BASIC loader
 * (at 0x5CCB) is done with once the program runs, and the program itself
 * (0x8000-0xF000) is full, so tables built at start-up go here. They are
 * read a few dozen times a frame, so the ULA's contention in this area
 * costs next to nothing.
 */
#ifndef LOWMEM_H
#define LOWMEM_H

#define LOW_SONG_ROWS  0x5D00           /* 2048: the music's rows, unpacked (music_song.c) */
#define LOW_SHIP_POLY  0x6500           /* 128 x POLY_BYTES(5) = 3584 */
#define LOW_FLAME_POLY 0x7300           /* 64 x POLY_BYTES(3) = 1152 */
#define LOW_ROCK_POLY  0x7780           /* 12 x POLY_BYTES(8) = 516 */
#define LOW_SX_TAB     0x7990           /* 320 */
#define LOW_SY_TAB     0x7AD0           /* 240 */
#define LOW_POINTS     0x7BC0           /* 2 x 192 */
#define LOW_END        0x7D40

#endif
