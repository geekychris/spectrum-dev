/* made by tools/gen_sound.py */
#ifndef SOUND_DATA_H
#define SOUND_DATA_H
#include <stdint.h>
typedef struct { uint8_t page; uint16_t addr; } SndRef;
static const SndRef snd_theme[7][2] = {
    { { 204, 0x4000 }, { 204, 0x4A5F } },
    { { 204, 0x4D64 }, { 204, 0x5B1B } },
    { { 204, 0x5CBD }, { 205, 0x4AAC } },
    { { 205, 0x4CA7 }, { 205, 0x5B77 } },
    { { 205, 0x5D9D }, { 206, 0x44FE } },
    { { 206, 0x47DB }, { 206, 0x5588 } },
    { { 206, 0x578F }, { 207, 0x45EC } },
};
static const SndRef snd_sfx[10] = {
    { 207, 0x4868 },
    { 207, 0x48AF },
    { 207, 0x48F5 },
    { 207, 0x4984 },
    { 207, 0x49A8 },
    { 207, 0x49CF },
    { 207, 0x4ABD },
    { 207, 0x4B6C },
    { 207, 0x4BDB },
    { 207, 0x4C50 },
};
#endif
