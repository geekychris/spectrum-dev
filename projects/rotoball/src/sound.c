/*
 * Music and effects (the original's sound.c: CLANG and PING on Paula), as
 * TurboSound register streams made by tools/gen_sound.py and played by the
 * SDK's aystream: music on chips 0 and 1, effects on chip 2, so an effect
 * never cuts the music (the original's single audio channel aborts the
 * sample playing).
 */
#include <stdint.h>
#include <z80.h>
#include "sound.h"
#include "aystream.h"

void snd_init(void) { as_init(); }

static void play2(uint8_t a, uint8_t b, uint8_t loop)
{
    as_play(0, streams[a].page, streams[a].addr, loop);
    as_play(1, streams[b].page, streams[b].addr, loop);
}

void snd_title_music(uint8_t on)
{
    if (on) {
        play2(ST_TITLE_A, ST_TITLE_B, 1);
    } else {
        as_stop(0);
        as_stop(1);
    }
}

void snd_jingle(void) { play2(ST_JINGLE_A, ST_JINGLE_B, 0); }

void snd_sfx(uint8_t st) { as_play(2, streams[st].page, streams[st].addr, 0); }

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
