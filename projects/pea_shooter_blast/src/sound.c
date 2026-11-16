/*
 * sound.c - the original's music and effects on TurboSound: its MOD ("Pea
 * Patrol", converted by sdk/tools/mod2ay.py into music.c) played by the
 * SDK's modplay on chips 0 and 1, and its effects (main.c build_sfx, as AY
 * streams made by tools/gen_sound.py) on chip 2 by aystream, one at a time
 * as ptplayer's mt_playfx plays them on a free channel.
 */
#include <stdint.h>
#include <z80.h>
#include "modplay.h"
#include "aystream.h"
#include "view.h"
#include "sound_data.h"

extern const mod_song pea_song;
static uint8_t playing;

void snd_init(void)
{
    ay_init();
    as_init();
}

void snd_music(uint8_t on)
{
    if (on == playing) return;
    playing = on;
    if (on) modplay_start(&pea_song);
    else modplay_stop();
}

void snd_play(uint8_t sfx)
{
    if (sfx < SFX_COUNT) as_play(2, sfx_streams[sfx].page, sfx_streams[sfx].addr, 0);
}

void snd_tick(void)
{
    if (playing) modplay_tick();
    as_tick();
}

/* what an AY chip plays now (test.py): register r of chip c */
uint8_t snd_reg(uint8_t c, uint8_t r)
{
    uint8_t v;
    z80_outp(0xFFFD, 0xFF - c);
    z80_outp(0xFFFD, r);
    v = z80_inp(0xFFFD);
    z80_outp(0xFFFD, 0xFF);
    return v;
}
