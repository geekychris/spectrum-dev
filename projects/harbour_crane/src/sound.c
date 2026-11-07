/*
 * Music and effects (the original's game_sound_init and a3_sfx) as
 * TurboSound register streams (tools/gen_sound.py), played by the SDK's
 * aystream: the title tune on chips 0 and 1, the effects on chip 2.
 * port: the original plays its effects on Paula and has no music.
 */
#include <stdint.h>
#include <z80.h>
#include "sound.h"
#include "aystream.h"

void snd_init(void) { as_init(); }

void snd_music(uint8_t on)
{
    if (on) {
        as_play(0, streams[ST_TITLE_A].page, streams[ST_TITLE_A].addr, 1);
        as_play(1, streams[ST_TITLE_B].page, streams[ST_TITLE_B].addr, 1);
    } else {
        as_stop(0);
        as_stop(1);
    }
}

void snd_play(uint8_t sfx) { as_play(2, streams[sfx].page, streams[sfx].addr, 0); }

void snd_tick(void) { as_tick(); }

uint8_t snd_reg(uint8_t c, uint8_t r)
{
    uint8_t v;
    z80_outp(0xFFFD, 0xFF - c);
    z80_outp(0xFFFD, r);
    v = z80_inp(0xFFFD);
    z80_outp(0xFFFD, 0xFF);
    return v;
}
