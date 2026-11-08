/*
 * sound.c - Boing Twist's sound on the Next's TurboSound: the original's
 * four effects (sound_ahi.c) as AY register streams made on the host
 * (tools/gen_sound.py), played by the SDK's aystream on chip 2 (a new one
 * replaces the one playing); port: a tune on chips 0 and 1 behind the title
 * and the select screen.
 */
#include <z80.h>
#include "aystream.h"
#include "sound.h"
#include "sound_data.h"

static uint8_t music;

void snd_init(void) { as_init(); }

void snd_music(uint8_t on)
{
    if (on == music) return;
    if (on) {
        as_play(0, music_streams[0].page, music_streams[0].addr, 1);
        as_play(1, music_streams[1].page, music_streams[1].addr, 1);
    } else {
        as_stop(0);
        as_stop(1);
    }
    music = on;
}

void snd_play(uint8_t sfx)
{
    if (sfx < SFX_COUNT) as_play(2, sfx_streams[sfx].page, sfx_streams[sfx].addr, 0);
}

void snd_tick(void) { as_tick(); }

/* register r of chip c, read back through port 0xFFFD (the test checks the sound) */
uint8_t snd_reg(uint8_t c, uint8_t r)
{
    uint8_t v;
    z80_outp(0xFFFD, 0xFF - c);
    z80_outp(0xFFFD, r);
    v = z80_inp(0xFFFD);
    z80_outp(0xFFFD, 0xFF);
    return v;
}
