/*
 * sound.c - music and effects on TurboSound (the original's a3_sfx calls):
 * AY register streams made on the host (tools/gen_sound.py), played by the
 * SDK's aystream: the music on chips 0 and 1, the effects on chip 2.
 */
#include <stdint.h>
#include <z80.h>
#include "aystream.h"
#include "breaker.h"
#include "sound_data.h"

static uint8_t track = 0xFF;

void snd_init(void) { as_init(); }

/* 0: none, 1: the title's tune, 2: the lighter one for play */
void snd_music(uint8_t t)
{
    if (t == track) return;
    track = t;
    if (!t) {
        as_stop(0);
        as_stop(1);
        return;
    }
    as_play(0, music_streams[t - 1][0].page, music_streams[t - 1][0].addr, 1);
    as_play(1, music_streams[t - 1][1].page, music_streams[t - 1][1].addr, 1);
}

void snd_play(uint8_t sfx)
{
    if (sfx < SFX_COUNT) as_play(2, sfx_streams[sfx].page, sfx_streams[sfx].addr, 0);
}

void snd_tick(void) { as_tick(); }

/* what an AY chip plays now (test.py checks the music and effects): register
   r of chip c, read back through port 0xFFFD */
uint8_t snd_reg(uint8_t c, uint8_t r)
{
    uint8_t v;
    z80_outp(0xFFFD, 0xFF - c);
    z80_outp(0xFFFD, r);
    v = z80_inp(0xFFFD);
    z80_outp(0xFFFD, 0xFF);
    return v;
}
