/*
 * sound.c - the original's music and effects on the Next's TurboSound:
 * rock_blaster.mod's two channels on chip 1 through the SDK's modplay (its
 * rows in a bank), the effects on chip 0. The original makes its effects
 * as short samples (main.c build_sfx: a falling blip, a thump, noise, a
 * three-note arpeggio, a sweep); here they are AY tones and noise with the
 * same shapes.
 */
#include <z80.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "modplay.h"
#include "game.h"
#include "orb.h"
#include "banks.h"

extern const mod_song oh_song;

#define P(hz) ((uint16_t)(109375UL / (hz)))     /* AY period at the Next's 1.75 MHz */

static const uint16_t powerup_notes[3] = { P(660), P(830), P(990) };   /* build_sfx's 20 / 25 / 30 */
static uint8_t powerup_step, powerup_left;
uint16_t snd_events;                            /* effects started (the test counts them) */
uint8_t snd_sounding[3];                        /* frames each chip had a channel up (main.c logs them) */

/* register r of chip c, read back through port 0xFFFD */
static uint8_t ay_reg(uint8_t c, uint8_t r)
{
    uint8_t v;
    z80_outp(0xFFFD, 0xFF - c);
    z80_outp(0xFFFD, r);
    v = z80_inp(0xFFFD);
    z80_outp(0xFFFD, 0xFF);
    return v;
}

void snd_init(void) { ay_init(); }

void snd_music(uint8_t on)
{
    ZXN_WRITE_MMU2(MUSIC_BANK * 2);
    if (on) modplay_start(&oh_song);
    else modplay_stop();
    ZXN_WRITE_MMU2(10);
    ay_chip(0);
}

void snd_tick(void)
{
    ZXN_WRITE_MMU2(MUSIC_BANK * 2);
    modplay_tick();                             /* leaves chip 0 selected */
    ZXN_WRITE_MMU2(10);
    if (powerup_left && !--powerup_left && powerup_step < 3) {
        ay_sfx(0, powerup_notes[powerup_step++], 13, 1, 0);
        powerup_left = powerup_step < 3 ? 6 : 0;
    }
    sound_tick();
    if ((ay_reg(0, 8) | ay_reg(0, 9) | ay_reg(0, 10)) & 15) snd_sounding[0]++;
    if ((ay_reg(1, 8) | ay_reg(1, 9)) & 15) snd_sounding[1]++;
}

void sfx_shoot(void)      { snd_events++; ay_sfx(0, P(1400), 11, 3, 6); }
void sfx_missile(void)    { snd_events++; ay_sfx(1, P(160), 13, 2, 12); }
void sfx_explode(void)    { snd_events++; ay_sfx(2, 0, 15, 1, 0); }
void sfx_player_hit(void) { snd_events++; ay_sfx(2, 0, 13, 2, 0); }
void sfx_hit(void)        { snd_events++; ay_sfx(1, P(700), 10, 3, 0); }
void sfx_door(void)       { snd_events++; ay_sfx(1, P(900), 12, 1, 14); }
void sfx_jump(void)       { snd_events++; ay_sfx(0, P(420), 10, 2, -10); }
void sfx_powerup(void)
{
    snd_events++;
    powerup_step = 0;
    powerup_left = 1;
}
