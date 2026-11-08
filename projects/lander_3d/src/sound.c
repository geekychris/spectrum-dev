/*
 * sound.c - Lander 3D's sound on the Next's TurboSound: the original's
 * music ("Quiet Descent", its MOD, through tools/make_music.py and the
 * SDK's modplay; the rows in a bank) on chips 1 and 2 behind the title, and
 * its effects on chip 0 (game.c game_sound_init: noise for the engine, the
 * thrusters and the crash, tones for the landing chime, the fuel warning
 * and the beep), as AY effects (ay_sfx) and short note lists.
 */
#include <z80.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "modplay.h"
#include "sound.h"

extern const mod_song ld_song;

#define P(hz) ((uint16_t)(109375UL / (hz)))  /* AY period at the Next's 1.75 MHz */

typedef struct { uint16_t period; uint8_t frames; } note;
/* the original's chimes: a3_tone 523, 659, 784 Hz (land); 880, 660 (fuel) */
static const note j_land[] = { { P(523), 5 }, { P(659), 5 }, { P(784), 14 }, { 0, 0 } };
static const note j_fuel[] = { { P(880), 5 }, { P(660), 12 }, { 0, 0 } };
static const note *jingle;
static uint8_t jingle_left, music_on;

void snd_init(void) { ay_init(); }

void snd_music(uint8_t on)
{
    if (on == music_on) return;
    music_on = on;
    ZXN_WRITE_REG(0x50, MUSIC_BANK * 2);
    if (on) modplay_start(&ld_song);
    else modplay_stop();
    ZXN_WRITE_REG(0x50, 0xFF);
    ay_chip(0);
}

void snd_play(uint8_t sfx)
{
    switch (sfx) {
    case SFX_THRUST: ay_sfx(2, 0, 9, 3, 0); break;           /* noise, quick */
    case SFX_RCS: ay_sfx(1, 0, 6, 3, 0); break;
    case SFX_LAND: jingle = j_land; jingle_left = 0; break;
    case SFX_CRASH: ay_sfx(2, 0, 15, 1, 0); ay_sfx(1, P(120), 13, 1, 20); break;
    case SFX_FUEL: jingle = j_fuel; jingle_left = 0; break;
    case SFX_BEEP: ay_sfx(0, P(1400), 12, 2, 0); break;
    case SFX_KEY: ay_sfx(0, P(1000), 10, 4, 0); break;
    }
}

void snd_tick(void)
{
    if (music_on) {
        ZXN_WRITE_REG(0x50, MUSIC_BANK * 2);
        modplay_tick();                  /* leaves chip 0 selected */
        ZXN_WRITE_REG(0x50, 0xFF);
    }
    if (jingle) {
        if (!jingle_left) {
            if (!jingle->frames) jingle = 0;
            else {
                ay_sfx(0, jingle->period, 13, 1, 0);
                jingle_left = jingle->frames;
                jingle++;
            }
        }
        if (jingle_left) jingle_left--;
    }
    sound_tick();
}

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
