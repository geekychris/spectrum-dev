/*
 * sound.c - Drone Courier's sound on the Next's TurboSound (port: the
 * original synthesises its effects into Paula samples at start-up, in
 * game.c game_sound_init()).
 *
 * Each effect is the original's: its tones (a3_tone: a triangle sweeping f0
 * to f1, fading over its last samples) and noise (a3_noise: decaying, its
 * smoothing a lower noise pitch, an attack), at the same lengths (11.8 kHz
 * samples -> 50 Hz frames) and loudness (Paula volume x amplitude -> AY
 * level, 2 steps a halving). They play on chip 2: channel A the events (an
 * effect cuts one of lower or equal priority, as a3_sfx's priorities), B
 * the gusts, C the rotors' hum.
 */
#include <stdint.h>
#include <z80.h>
#include "sound.h"
#include "zxgame.h"
#include "nextgfx.h"

typedef struct { uint8_t frames; uint16_t f0, f1; uint8_t noise, level, fade; } seg_t;   /* noise: 0 tone, else the AY noise period */
typedef struct { const seg_t *s; uint8_t n, pri; } fx_t;

/* game_sound_init(), segment by segment: tone(n samples, f0, f1, amp, fade samples) */
static const seg_t s_pickup[] = { {3, 660, 660, 0, 13, 0}, {4, 990, 990, 0, 13, 3} };
static const seg_t s_deliver[] = { {5, 523, 523, 0, 14, 0}, {5, 659, 659, 0, 14, 0}, {5, 784, 784, 0, 14, 0},
                                   {5, 1046, 1046, 0, 14, 4} };
static const seg_t s_bump[] = { {4, 0, 0, 8, 14, 4} };
static const seg_t s_broken[] = { {6, 0, 0, 4, 15, 6}, {6, 300, 120, 0, 14, 5} };
static const seg_t s_late[] = { {13, 400, 200, 0, 13, 6} };
static const seg_t s_gust[] = { {5, 0, 0, 31, 9, 0}, {5, 0, 0, 31, 10, 5} };
static const seg_t s_day[] = { {7, 784, 784, 0, 14, 0}, {7, 988, 988, 0, 14, 0}, {7, 1175, 1175, 0, 14, 4} };
static const seg_t s_over[] = { {25, 440, 110, 0, 14, 13} };
static const seg_t s_win[] = { {6, 784, 784, 0, 14, 0}, {6, 1046, 1046, 0, 14, 0}, {13, 1568, 1568, 0, 14, 8} };
static const seg_t s_hum[] = { {3, 180, 190, 0, 8, 0} };

static const fx_t fx[SFX_COUNT] = {
    {s_pickup, 2, 30}, {s_deliver, 4, 50}, {s_bump, 1, 30}, {s_broken, 2, 50}, {s_late, 1, 45},
    {s_gust, 2, 8}, {s_day, 3, 55}, {s_over, 1, 60}, {s_win, 3, 60}, {s_hum, 1, 2},
};

typedef struct { const seg_t *s; uint8_t n, i, t, pri, attack; } chan_t;
static chan_t ch[3];
static uint8_t mixer2 = 0x3F;

static void reg(uint8_t chip, uint8_t r, uint8_t v)
{
    ay_chip(chip);
    ay_write(r, v);
}

uint8_t snd_reg(uint8_t chip, uint8_t r)
{
    ay_chip(chip);
    z80_outp(0xFFFD, r);
    return z80_inp(0xFFFD);
}

void snd_init(void)
{
    uint8_t c, r;
    for (c = 0; c < 3; c++) {
        ay_chip(c);
        for (r = 0; r < 14; r++) ay_write(r, 0);
        ay_write(7, 0x3F);
        ch[c].n = 0;
    }
    mixer2 = 0x3F;
}

void snd_play(uint8_t id)
{
    uint8_t c = id == SFX_GUST ? 1 : id == SFX_HUM ? 2 : 0;
    chan_t *k = &ch[c];
    if (k->n && fx[id].pri < k->pri) return;
    k->s = fx[id].s;
    k->n = fx[id].n;
    k->pri = fx[id].pri;
    k->i = 0;
    k->t = 0;
    k->attack = id == SFX_GUST;           /* a3_noise(.., attack 1200): the first segment rises */
}

static void chan_tick(uint8_t c)
{
    chan_t *k = &ch[c];
    const seg_t *s;
    uint8_t lvl, tone_bit = 1 << c, noise_bit = 8 << c, left;
    if (!k->n) return;
    if (k->i == k->n) {
        k->n = 0;
        mixer2 |= tone_bit | noise_bit;
        reg(2, 7, mixer2);
        reg(2, 8 + c, 0);
        return;
    }
    s = &k->s[k->i];
    lvl = s->level;
    left = s->frames - k->t;
    if (s->fade && left <= s->fade) lvl = (uint8_t)((uint16_t)lvl * left / (s->fade + 1));
    if (k->attack && k->i == 0) lvl = (uint8_t)((uint16_t)lvl * (k->t + 1) / s->frames);
    if (s->noise) {
        mixer2 = (mixer2 | tone_bit) & ~noise_bit;
        reg(2, 6, s->noise);
    } else {
        uint16_t f = s->f0 + (int16_t)(((int32_t)((int16_t)s->f1 - (int16_t)s->f0) * k->t) / s->frames);
        uint16_t p = (uint16_t)(110837UL / f);
        mixer2 = (mixer2 | noise_bit) & ~tone_bit;
        reg(2, c * 2, (uint8_t)p);
        reg(2, c * 2 + 1, (uint8_t)(p >> 8));
    }
    reg(2, 7, mixer2);
    reg(2, 8 + c, lvl);
    if (++k->t == s->frames) {
        k->t = 0;
        k->i++;
    }
}

/* ------------------------------------------------------------ the tune */
/* port: the original has no music. A short tune for the title (and,
   quieter, in flight) on chips 0 and 1: a melody and a bass line. */
typedef struct { uint8_t note, len; } note_t;      /* MIDI note (0 rest), frames */
static const note_t melody[] = {
    {72, 12}, {76, 12}, {79, 12}, {76, 12}, {74, 12}, {77, 12}, {81, 24},
    {79, 12}, {77, 12}, {76, 12}, {74, 12}, {72, 24}, {0, 24},
    {72, 12}, {76, 12}, {79, 12}, {84, 12}, {83, 12}, {79, 12}, {76, 24},
    {77, 12}, {76, 12}, {74, 12}, {71, 12}, {72, 36}, {0, 12}, {0, 0}
};
static const note_t bass[] = {
    {48, 24}, {55, 24}, {50, 24}, {53, 24}, {55, 24}, {43, 24}, {48, 24}, {48, 24},
    {48, 24}, {52, 24}, {53, 24}, {55, 24}, {53, 24}, {55, 24}, {48, 24}, {48, 24}, {0, 0}
};
static uint8_t mus_on, mel_i, mel_t, bass_i, bass_t;

static void mus_note(uint8_t chip, uint8_t note, uint8_t vol)
{
    uint16_t p = note ? AY_NOTE(note) : 0;
    reg(chip, 0, (uint8_t)p);
    reg(chip, 1, (uint8_t)(p >> 8));
    reg(chip, 8, note ? vol : 0);
}

void snd_music(uint8_t on)
{
    if (on == mus_on) return;
    mus_on = on;
    mel_i = mel_t = bass_i = bass_t = 0;
    reg(0, 7, on ? 0x3E : 0x3F);         /* tone A only */
    reg(1, 7, on ? 0x3E : 0x3F);
    if (!on) { reg(0, 8, 0); reg(1, 8, 0); }
}

static void mus_tick(void)
{
    uint8_t quiet = mus_on == 2 ? 4 : 0;
    if (!mus_on) return;
    if (mel_t == 0) {
        if (!melody[mel_i].len) mel_i = 0;
        mus_note(0, melody[mel_i].note, 12 - quiet);
        mel_t = melody[mel_i].len;
        mel_i++;
    } else if (mel_t == 2) reg(0, 8, 9 - quiet);       /* a short release */
    mel_t--;
    if (bass_t == 0) {
        if (!bass[bass_i].len) bass_i = 0;
        mus_note(1, bass[bass_i].note, 11 - quiet);
        bass_t = bass[bass_i].len;
        bass_i++;
    }
    bass_t--;
}

void snd_tick(void)
{
    uint8_t c;
    mus_tick();
    for (c = 0; c < 3; c++) chan_tick(c);
}
