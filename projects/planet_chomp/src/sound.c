/*
 * Sound (the original's sfx.h on TurboSound, tools/gen_sound.py): the
 * crumbs on chip 2, the other effects on chip 0, the ambience on chip 1, and
 * (port) the title tune on chips 0 and 1. game.c asks; the main loop calls
 * snd_tick once per frame that passed.
 */
#include <stdint.h>
#include "aystream.h"
#include "game.h"
#include "sound_data.h"
#include "sound.h"

static uint8_t crumb_flip, amb_now = 0xFF, title_on;

void snd_init(void) { as_init(); }

void sfx_play(uint8_t id)
{
    if (title_on) snd_title(0);
    as_play(0, sfx_streams[id].page, sfx_streams[id].addr, 0);
}

void sfx_crumb(void)
{
    crumb_flip = !crumb_flip;
    as_play(2, sfx_streams[crumb_flip ? SFX_CRUMB_A : SFX_CRUMB_B].page,
            sfx_streams[crumb_flip ? SFX_CRUMB_A : SFX_CRUMB_B].addr, 0);
}

void sfx_ambience(uint8_t which)
{
    if (which == amb_now) return;
    amb_now = which;
    if (which == 0) as_stop(1);
    else {
        uint8_t id = which == 2 ? SFX_FRIGHT : SFX_HUM;
        title_on = 0;
        as_play(1, sfx_streams[id].page, sfx_streams[id].addr, 1);
    }
}

void snd_title(uint8_t on)
{
    if (on == title_on) return;
    title_on = on;
    if (on) {
        amb_now = 0xFF;
        as_play(0, title_streams[0].page, title_streams[0].addr, 1);
        as_play(1, title_streams[1].page, title_streams[1].addr, 1);
    } else {
        as_stop(0);
        as_stop(1);
    }
}

void snd_tick(void) { as_tick(); }
