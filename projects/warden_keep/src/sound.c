/*
 * sound.c - Warden Keep's sound on the Next's TurboSound (port: the original has none):
 * effects and a tune, as AY register streams made on the host
 * (tools/gen_sound.py), played by the SDK's aystream: the music on chips 0
 * and 1, the effects on chip 2 (a new one replaces the one playing).
 */
#include <z80.h>
#include "aystream.h"
#include "sound.h"
#include "sound_data.h"

static uint8_t music;

void snd_init(void) { as_init(); }

/* the music: behind the title at full volume, in play 4 steps quieter */
void snd_music(uint8_t m)
{
    if (m == music) return;
    if (m == MUS_NONE) {
        as_stop(0);
        as_stop(1);
    } else {
        if (music == MUS_NONE) {
            as_play(0, music_streams[0].page, music_streams[0].addr, 1);
            as_play(1, music_streams[1].page, music_streams[1].addr, 1);
        }
        as_atten(0, m == MUS_PLAY ? 4 : 0);
        as_atten(1, m == MUS_PLAY ? 4 : 0);
    }
    music = m;
}

void snd_play(uint8_t sfx)
{
    if (sfx < SFX_COUNT) as_play(2, sfx_streams[sfx].page, sfx_streams[sfx].addr, 0);
}

void snd_tick(void) { as_tick(); }

/* what an AY chip plays now (the test checks the music and the effects):
   register r of chip c, read back through port 0xFFFD */
uint8_t snd_reg(uint8_t c, uint8_t r)
{
    uint8_t v;
    z80_outp(0xFFFD, 0xFF - c);
    z80_outp(0xFFFD, r);
    v = z80_inp(0xFFFD);
    z80_outp(0xFFFD, 0xFF);
    return v;
}
