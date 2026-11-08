/*
 * The original's sound (sound_ahi.c / sound_paula.c) on TurboSound: the
 * seven themes on chips 0 and 1, the effects on chip 2 (tools/gen_sound.py
 * made them from the original's definitions), and the rolling rumble on
 * chip 1's channel C: noise whose volume eases towards the marble's speed
 * and whose pitch rises with it, as the original pitches its loop.
 */
#include <arch/zxn.h>
#include <z80.h>
#include "nextgfx.h"
#include "aystream.h"
#include "rs.h"
#include "sound.h"
#include "sound_data.h"

static int8_t theme = -1;
static fix roll_target;
static int16_t roll_vol;                 /* eased, 0..ROLL_MAX << 4 */
#define ROLL_MAX 11

void snd_init(void)
{
    as_init();
}

void snd_play(int sfx)
{
    if (sfx < 0 || sfx >= SFX_COUNT) return;
    as_play(2, snd_sfx[sfx].page, snd_sfx[sfx].addr, 0);
}

void snd_music(int t)
{
    if (t == theme) return;
    theme = (int8_t)t;
    if (t < 0 || t > 6) {
        as_stop(0);
        as_stop(1);
        return;
    }
    as_play(0, snd_theme[t][0].page, snd_theme[t][0].addr, 1);
    as_play(1, snd_theme[t][1].page, snd_theme[t][1].addr, 1);
}

void snd_roll(fix speed01) { roll_target = speed01; }

static void ay_write(uint8_t r, uint8_t v)
{
    z80_outp(0xFFFD, r);
    z80_outp(0xBFFD, v);
}

/* one frame: the streams, then the rumble (after them: a stream's loop point
   rewrites chip 1's registers) */
void snd_frame(void)
{
    int16_t want = (int16_t)((roll_target * (ROLL_MAX << 4)) >> 12);
    as_tick();
    roll_vol += (want - roll_vol) / 4;
    ay_chip(1);
    ay_write(6, (uint8_t)(26 - (roll_target * 18 >> 12)));   /* faster: higher */
    ay_write(10, (uint8_t)(roll_vol >> 4));
}
