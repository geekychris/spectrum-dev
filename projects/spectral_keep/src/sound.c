/*
 * Music and effects (the original's sound.c, for TurboSound): the original's
 * songs and effects as AY register streams (tools/gen_sound.py), played by
 * the SDK's aystream player: music on chips 0 and 1, effects on chip 2.
 */
#include "keep.h"
#include "sound.h"
#include "sound_data.h"
#include "aystream.h"

static uint8_t track = T_NONE, enabled = 1;
static int16_t duck;

void snd_init(void) { as_init(); }

static void start(void)
{
    if (track == T_NONE || !enabled) {
        as_stop(0);
        as_stop(1);
        return;
    }
    as_play(0, music_streams[track][0].page, music_streams[track][0].addr, 1);
    as_play(1, music_streams[track][1].page, music_streams[track][1].addr, 1);
}

void snd_music(uint8_t t)
{
    if (t == track) return;
    track = t;
    start();
}

void snd_music_enable(uint8_t on)
{
    enabled = on;
    start();
}

void snd_play(uint8_t sfx)
{
    if (sfx < SFX_COUNT) as_play(2, sfx_streams[sfx].page, sfx_streams[sfx].addr, 0);
}

/* music quieter for a while (someone talks, a relic) */
void snd_duck(int16_t ticks)
{
    duck = ticks;
    as_atten(0, 5);
    as_atten(1, 5);
}

void snd_tick(void)
{
    as_tick();
    if (duck > 0 && --duck == 0) {
        as_atten(0, 0);
        as_atten(1, 0);
    }
}
