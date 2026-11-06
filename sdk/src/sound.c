/* Beeper helpers and the AY (see zxgame.h). */
#include <stdint.h>
#include <z80.h>
#include "zxgame.h"

uint8_t snd_border;
uint8_t ay_present;

void beep_sweep(uint16_t from, uint16_t to, uint8_t step, uint8_t cycles)
{
    if (from < to) {
        for (; from < to; from += step) beep(from, cycles);
    }
    else {
        for (; from > to && from > step; from -= step) beep(from, cycles);
    }
}

/* ---- AY-3-8912 (128K/+2/+3, the Next's TurboSound chip 0) ---- */

uint8_t ay_mixer = 0x3F;             /* register 7 of chip 0: all tone + noise off (modplay.c shares it) */

void ay_write(uint8_t reg, uint8_t val)
{
    z80_outp(0xFFFD, reg);
    z80_outp(0xBFFD, val);
}

void ay_init(void)
{
    uint8_t i;
#ifdef __ZXNEXT
    z80_outp(0xFFFD, 0xFF);          /* TurboSound: select chip 0, both channels */
#endif
    /* a 48K has no AY: reading 0xFFFD returns the floating bus */
    ay_write(1, 0x0A);
    z80_outp(0xFFFD, 1);
    ay_present = (z80_inp(0xFFFD) & 0x0F) == 0x0A;
    if (!ay_present) return;
    for (i = 0; i < 14; i++) ay_write(i, 0);
    ay_mixer = 0x3F;
    ay_write(7, ay_mixer);
}

void ay_tone(uint8_t ch, uint16_t period, uint8_t vol)
{
    if (!ay_present) return;
    ay_write(ch * 2, period & 0xFF);
    ay_write(ch * 2 + 1, (period >> 8) & 0x0F);
    ay_write(8 + ch, vol & 15);
    ay_mixer = (ay_mixer | (8 << ch)) & ~(1 << ch);     /* tone on, noise off */
    ay_write(7, ay_mixer);
}

void ay_noise(uint8_t ch, uint8_t period, uint8_t vol)
{
    if (!ay_present) return;
    ay_write(6, period & 0x1F);
    ay_write(8 + ch, vol & 15);
    ay_mixer = (ay_mixer | (1 << ch)) & ~(8 << ch);     /* noise on, tone off */
    ay_write(7, ay_mixer);
}

void ay_off(uint8_t ch)
{
    if (!ay_present) return;
    ay_write(8 + ch, 0);
    ay_mixer |= (9 << ch);
    ay_write(7, ay_mixer);
}

/* ---- effects and music, advanced by sound_tick() once per frame ---- */

typedef struct { uint16_t period; int8_t sweep; uint8_t vol, decay, active; } sfx_state;
typedef struct { const ay_note *start, *at; uint8_t left, vol, active; } track_state;
static sfx_state sfx[3];
static track_state music[3];

void ay_sfx(uint8_t ch, uint16_t period, uint8_t vol, uint8_t decay, int8_t sweep)
{
    if (!ay_present || ch > 2) return;
    sfx[ch].period = period;
    sfx[ch].vol = vol;
    sfx[ch].decay = decay ? decay : 1;
    sfx[ch].sweep = sweep;
    sfx[ch].active = 1;
    if (period) ay_tone(ch, period, vol);
    else ay_noise(ch, 15, vol);
}

void music_play(uint8_t ch, const ay_note *track, uint8_t vol)
{
    if (ch > 2) return;
    music[ch].start = music[ch].at = track;
    music[ch].left = 0;
    music[ch].vol = vol;
    music[ch].active = 1;
}

void music_stop(void)
{
    uint8_t c;
    for (c = 0; c < 3; c++) {
        music[c].active = 0;
        if (!sfx[c].active) ay_off(c);
    }
}

void sound_tick(void)
{
    uint8_t c;
    if (!ay_present) return;
    for (c = 0; c < 3; c++) {
        sfx_state *s = &sfx[c];
        track_state *t = &music[c];
        if (s->active) {                         /* effects win over music */
            if (s->vol <= s->decay) {
                s->active = 0;
                ay_off(c);
            }
            else {
                s->vol -= s->decay;
                s->period += s->sweep;
                if (s->period) ay_tone(c, s->period, s->vol);
                else ay_noise(c, 15, s->vol);
            }
            if (t->active && t->left) t->left--;
            continue;
        }
        if (!t->active) continue;
        if (t->left) {
            t->left--;
            if (t->left == 1) ay_write(8 + c, t->vol > 3 ? t->vol - 3 : 0);   /* release */
            continue;
        }
        if (t->at->len == 0) t->at = t->start;       /* loop */
        if (t->at->len == 0) { t->active = 0; continue; }
        if (t->at->period) ay_tone(c, t->at->period, t->vol);
        else ay_off(c);
        t->left = t->at->len;
        t->at++;
    }
}
