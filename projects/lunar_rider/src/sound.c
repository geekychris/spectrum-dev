/*
 * Lunar Rider - sound on the Next's TurboSound (port: replaces sound.c's
 * Paula samples).
 *
 * The original plays its "bouncy bass" (16 notes, 7 frames each, a square
 * wave) on Paula channel 3 all the time, and its effects as short looped
 * samples on channels 0-2 for a few frames. Here the bass is a square tone on
 * AY chip 0, channel A, and the effects are frame-by-frame tone / noise
 * tables on chip 1's channels A-C (the original's channels 0-2, so an effect
 * cuts only the one on its own channel, as on the Amiga), the same lengths
 * and pitches as the samples (their frequencies worked out from the Paula
 * periods and the waveforms' cycle lengths).
 */
#include <stdint.h>
#include <z80.h>
#include "sound.h"
#include "zxgame.h"
#include "nextgfx.h"

typedef struct { uint16_t period; uint8_t vol; uint8_t noise; } sfx_step;   /* noise: 0 none, 1 noise only, 2 tone+noise */

/* sound_shoot(): a 3284 Hz square blip, 3 frames */
static const sfx_step fx_shoot[] = {{33, 13, 0}, {33, 10, 0}, {33, 6, 0}};
/* sound_explode(): decaying noise, 10 frames */
static const sfx_step fx_explode[] = {
    {6, 15, 1}, {8, 14, 1}, {10, 13, 1}, {12, 11, 1}, {14, 10, 1},
    {16, 8, 1}, {18, 7, 1}, {20, 5, 1}, {24, 4, 1}, {28, 2, 1},
};
/* sound_jump(): a chirp rising from ~590 to ~1970 Hz, 8 frames */
static const sfx_step fx_jump[] = {
    {185, 12, 0}, {150, 12, 0}, {123, 11, 0}, {104, 10, 0}, {88, 9, 0}, {75, 8, 0}, {64, 7, 0}, {56, 6, 0},
};
/* sound_checkpoint(): the rising four-note arpeggio, 20 frames */
static const sfx_step fx_checkpoint[] = {
    {123, 13, 0}, {99, 13, 0}, {74, 13, 0}, {62, 13, 0}, {123, 12, 0}, {99, 12, 0}, {74, 12, 0},
    {62, 12, 0}, {123, 11, 0}, {99, 11, 0}, {74, 11, 0}, {62, 11, 0}, {123, 10, 0}, {99, 10, 0},
    {74, 10, 0}, {62, 10, 0}, {74, 9, 0}, {62, 9, 0}, {62, 8, 0}, {62, 7, 0},
};
/* sound_death(): a buzz falling from ~2950 to ~680 Hz with noise, 15 frames */
static const sfx_step fx_death[] = {
    {37, 14, 2}, {45, 14, 2}, {53, 13, 2}, {61, 12, 2}, {70, 12, 2}, {78, 11, 2}, {86, 10, 2}, {95, 9, 2},
    {103, 9, 2}, {112, 8, 2}, {120, 7, 2}, {128, 6, 2}, {137, 5, 2}, {145, 5, 2}, {160, 4, 2},
};

typedef struct { const sfx_step *s; uint8_t n, i; } slot_t;
static slot_t slot[3];
static uint8_t mixer[2] = {0x3F, 0x3F};

/* the original's bass: C3 C3 G3 G3 A3 A3 F3 F3 E3 E3 G3 G3 C3 D3 E3 F3 */
static const uint8_t bass[16] = {48, 48, 55, 55, 57, 57, 53, 53, 52, 52, 55, 55, 48, 50, 52, 53};
#define BASS_TEMPO 7
#define BASS_VOL   11                   /* Paula volume 30 of 64 */
static uint8_t bass_pos, bass_timer, music_on;

static void reg(uint8_t chip, uint8_t r, uint8_t v)
{
    ay_chip(chip);
    ay_write(r, v);
}

static void set_mix(uint8_t chip)
{
    reg(chip, 7, mixer[chip]);
}

void snd_init(void)
{
    uint8_t c, r;
    for (c = 0; c < 3; c++) {
        ay_chip(c);
        for (r = 0; r < 14; r++) ay_write(r, 0);
        ay_write(7, 0x3F);
    }
    mixer[0] = mixer[1] = 0x3F;
    slot[0].n = slot[1].n = slot[2].n = 0;
    bass_pos = 0;
    bass_timer = 0;
    music_on = 1;
}

static void play(uint8_t ch, const sfx_step *s, uint8_t n)
{
    slot[ch].s = s;
    slot[ch].n = n;
    slot[ch].i = 0;
}

void snd_shoot(void) { play(0, fx_shoot, sizeof(fx_shoot) / sizeof(fx_shoot[0])); }
void snd_explode(void) { play(1, fx_explode, sizeof(fx_explode) / sizeof(fx_explode[0])); }
void snd_jump(void) { play(2, fx_jump, sizeof(fx_jump) / sizeof(fx_jump[0])); }
void snd_checkpoint(void) { play(2, fx_checkpoint, sizeof(fx_checkpoint) / sizeof(fx_checkpoint[0])); }
void snd_death(void) { play(1, fx_death, sizeof(fx_death) / sizeof(fx_death[0])); }

void snd_music(uint8_t on)
{
    music_on = on;
    if (!on) {
        reg(0, 8, 0);
        bass_timer = 0;
    }
}

/* once per logic frame: the effects' next step, the bass */
void snd_tick(void)
{
    uint8_t ch;
    for (ch = 0; ch < 3; ch++) {
        slot_t *s = &slot[ch];
        uint8_t m = mixer[1];
        if (!s->n) continue;
        if (s->i >= s->n) {             /* done: silence the channel */
            reg(1, 8 + ch, 0);
            s->n = 0;
            continue;
        }
        {
            const sfx_step *st = &s->s[s->i++];
            m |= (9 << ch);             /* tone and noise off */
            if (st->noise != 1) {
                reg(1, ch * 2, (uint8_t)st->period);
                reg(1, ch * 2 + 1, (uint8_t)(st->period >> 8));
                m &= ~(1 << ch);
            }
            if (st->noise) {
                reg(1, 6, st->noise == 1 ? (uint8_t)st->period : 8);
                m &= ~(8 << ch);
            }
            reg(1, 8 + ch, st->vol);
        }
        if (m != mixer[1]) {
            mixer[1] = m;
            set_mix(1);
        }
    }
    if (music_on && !bass_timer--) {
        uint16_t p = AY_NOTE(bass[bass_pos]);
        bass_timer = BASS_TEMPO - 1;
        reg(0, 0, (uint8_t)p);
        reg(0, 1, (uint8_t)(p >> 8));
        reg(0, 8, BASS_VOL);
        if (mixer[0] != 0x3E) {
            mixer[0] = 0x3E;            /* chip 0: tone A only */
            set_mix(0);
        }
        if (++bass_pos == 16) bass_pos = 0;
    }
    ay_chip(0);
}

uint8_t snd_reg(uint8_t chip, uint8_t r)
{
    uint8_t v;
    ay_chip(chip);
    z80_outp(0xFFFD, r);
    v = z80_inp(0xFFFD);
    ay_chip(0);
    return v;
}
