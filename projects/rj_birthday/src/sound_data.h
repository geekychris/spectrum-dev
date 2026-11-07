/* made by tools/gen_sound.py - do not edit */
#ifndef SOUND_DATA_H
#define SOUND_DATA_H
#include <stdint.h>
#define SND_PAGE 54      /* the streams, addresses with it in slot 2 */
#define SND_RNG_AFTER_INIT 454198353UL   /* the original's snd_rng after sound_init */

enum { SND_DOORBELL, SND_SLIDE, SND_CHOMP, SND_DING, SND_BUZZER, SND_CHEER, SND_PICKUP, SND_CRASH, SND_PARTY, SND_ARCADE_A, SND_VOICE0, SND_VOICE1, SND_VOICE2, SND_VOICE3, SND_COUNT };
static const uint16_t snd_addr[SND_COUNT] = { 0x4000, 0x4020, 0x4038, 0x4050, 0x4068, 0x4083, 0x40a6, 0x40be, 0x40dc, 0x410f, 0x413f, 0x41be, 0x426b, 0x4321 };
static const uint8_t snd_pri[SND_COUNT] = { 30, 20, 25, 40, 35, 50, 25, 45, 55, 60, 127, 127, 127, 127 };
static const uint16_t voice_size[4] = { 9026, 13460, 12184, 9474 };   /* the clips' bytes */
#endif
