/*
 * sound.c - Dot Chase's sounds: the original's sound.c on the AY (128K) or
 * the beeper (48K).
 *
 * port: the original plays 32-byte waveforms on Paula; with that length an
 * Amiga period is, to within 0.01%, the same AY tone period, so the notes are
 * the original's numbers. Channels as in the original: 0 = seed pips,
 * 1 = sequences (catch, lose, bonus, extra life), 2 = the crickets and the
 * scared-spider arpeggio (the original's channels 2 and 3 share one here).
 * The beeper stops the CPU while it sounds, so on a 48K each frame plays one
 * slice of the most important sound (a sequence note > a pip > the
 * arpeggio > a cricket chirp): a pip ~4,000 T-states, a chirp ~1,500, a
 * sequence note (catching a spider, losing a life, a bonus) a third of a
 * frame, which costs frames while it plays (the logic keeps its 50 Hz).
 */
#include <stdint.h>
#include "zxgame.h"
#include "sound.h"

static const uint16_t pip_notes[] = { 339, 285, 254, 214, 190, 214, 254, 285 };
#define PIP_LEN 8
static const uint16_t lose_notes[] = { 1, 1, 0, 381, 0, 453, 0, 508, 508, 0, 678, 678, 678 };
static const uint16_t catch_notes[] = { 1, 226, 190, 151, 113 };
static const uint16_t bonus_notes[] = { 285, 214, 170 };
static const uint16_t extra_notes[] = { 254, 0, 254, 190, 151, 127 };
static const uint16_t arp[4] = { 254, 214, 170, 214 };

static uint8_t pip_pos;
static uint16_t pip_now;             /* a pip to play this frame (beeper) */

static const uint16_t *seq_notes;
static uint8_t seq_len, seq_pos, seq_timer, seq_speed;
static uint16_t seq_note;            /* sounding now: period, 1 = noise, 0 = quiet */
static uint8_t seq_left;             /* frames it still sounds */

static uint8_t pulse_on, pulse_level, pulse_timer, chirp;
static uint8_t power_on, power_phase;
static uint16_t arp_now;

/* the beeper's period for an Amiga/AY period, an octave up (short slices) */
static uint16_t beeper_period(uint16_t p)
{
    return p + p - (p >> 5) - 7;      /* (31.58 p / 2 - 60) / 8 */
}

/* a slice of about `t` T-states of tone */
static void beep_for(uint16_t p, uint16_t t)
{
    uint16_t b = beeper_period(p);
    uint16_t c = t / (8 * b + 60);
    beep(b, c ? c : 1);
}

static void start_seq(const uint16_t *notes, uint8_t len, uint8_t speed)
{
    seq_notes = notes;
    seq_len = len;
    seq_pos = 0;
    seq_timer = speed - 1;           /* the first note on the next tick */
    seq_speed = speed;
}

void snd_seed(void)
{
    if (ay_present) ay_sfx(0, pip_notes[pip_pos], 11, 5, 0);
    else pip_now = pip_notes[pip_pos];
    pip_pos = (pip_pos + 1) % PIP_LEN;
}

void snd_eat_spider(void) { start_seq(catch_notes, 5, 3); }
void snd_bonus(void)      { start_seq(bonus_notes, 3, 4); }
void snd_extra_life(void) { start_seq(extra_notes, 6, 5); }

void snd_die(void)
{
    start_seq(lose_notes, 13, 5);
    pulse_on = 0;
    power_on = 0;
    if (ay_present) ay_off(2);
}

void snd_power(void)
{
    power_on = 1;
    power_phase = 0;
}

void snd_power_off(void)
{
    if (!power_on) return;
    power_on = 0;
    arp_now = 0;
    if (ay_present) ay_off(2);
}

void snd_set_pulse(uint8_t level)
{
    pulse_on = 1;
    pulse_level = level;
}

void snd_pulse_off(void)
{
    if (pulse_on && ay_present && !power_on) ay_off(2);
    pulse_on = 0;
}

void snd_stop(void)
{
    seq_notes = 0;
    seq_left = 0;
    pulse_on = power_on = 0;
    pip_now = arp_now = 0;
    if (ay_present) { ay_off(0); ay_off(1); ay_off(2); }
}

/* one 50 Hz tick of the original's sound_update() */
void snd_tick(void)
{
    if (seq_left && !--seq_left && ay_present) ay_off(1);

    if (seq_notes) {
        seq_timer++;
        if (seq_timer >= seq_speed) {
            uint16_t n = seq_notes[seq_pos];
            seq_timer = 0;
            seq_note = n;
            seq_left = n ? seq_speed - 1 : 0;
            if (ay_present) {
                if (n == 1) ay_noise(1, 12, 13);
                else if (n) ay_tone(1, n, 13);
                else ay_off(1);
            }
            seq_pos++;
            if (seq_pos >= seq_len) seq_notes = 0;
        }
    }

    /* Crickets: three quick chirps, then a pause that shortens as
     * the seeds run out. Quiet while the arpeggio plays. */
    if (pulse_on && !power_on) {
        uint8_t gap = pulse_level == 0 ? 48 : pulse_level == 1 ? 34 : 22;
        pulse_timer++;
        if (pulse_timer == 1 || pulse_timer == 4 || pulse_timer == 7) {
            if (ay_present) ay_sfx(2, 120, 6, 3, 0);
            else chirp = 1;
        }
        if (pulse_timer >= gap) pulse_timer = 0;
    }

    /* Scared spiders: a bubbling four-note arpeggio */
    if (power_on) {
        power_phase++;
        if ((power_phase % 3) == 1) {
            arp_now = arp[(power_phase / 3) & 3];
            if (ay_present) ay_tone(2, arp_now, 8);
        }
    }
}

/* once a frame, after the screen is drawn */
void snd_frame(void)
{
    if (ay_present) {
        sound_tick();                /* the SDK's effect decay */
        return;
    }
    if (seq_left && seq_note > 1) beep_for(seq_note, 22000);
    else if (seq_left && seq_note == 1) beep_noise(400);
    else if (pip_now) beep_for(pip_now, 4000);
    else if (power_on && arp_now) { beep_for(arp_now, 2000); arp_now = 0; }
    else if (chirp) beep_for(120, 1500);
    pip_now = 0;
    chirp = 0;
}
