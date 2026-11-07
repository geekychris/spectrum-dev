/*
 * sound.c - the original's music and effects on the Next's TurboSound:
 * "Tower Climb" (its MOD, tools/make_music.py) on chips 1 and 2 through
 * the SDK's modplay (its rows in a bank), the effects on chip 0 (ay_sfx: a tone or noise that
 * decays and sweeps). The original's effects are tones and noise with a
 * pitch slide (game.c game_sound_init); the clear, level and win jingles
 * are short note lists played here.
 */
#include <z80.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "modplay.h"
#include "stack.h"

extern const mod_song st_song;
extern const uint8_t st_song_page;     /* the song's rows: in slot 0 while it plays */

#define P(hz) ((uint16_t)(109375UL / (hz)))  /* AY period at the Next's 1.75 MHz */

typedef struct { uint16_t period; uint8_t frames; } note;
static const note j_clear[] = { { P(523), 5 }, { P(659), 5 }, { P(1046), 12 }, { 0, 0 } };
static const note j_level[] = { { P(392), 6 }, { P(523), 6 }, { P(784), 14 }, { 0, 0 } };
static const note j_win[] = { { P(523), 6 }, { P(659), 6 }, { P(784), 6 }, { P(1046), 15 }, { 0, 0 } };
static const note *jingle;
uint8_t snd_sounding[3];                /* frames each chip had a channel up, since snd_reg's caller cleared them */
static uint8_t jingle_left;

void snd_init(void)
{
    ay_init();
}

void snd_music(uint8_t on)
{
    ZXN_WRITE_REG(0x50, st_song_page);
    if (on) modplay_start(&st_song);
    else modplay_stop();
    ZXN_WRITE_REG(0x50, 0xFF);
    ay_chip(0);
}

static void play_jingle(const note *j)
{
    jingle = j;
    jingle_left = 0;
}

void snd_event(uint8_t type, int16_t a)
{
    (void)a;
    switch (type) {
    case STE_MOVE: ay_sfx(0, P(900), 9, 3, 0); break;
    case STE_ROTATE: case STE_KICK: ay_sfx(1, P(700), 11, 2, -6); break;
    case STE_DROP: ay_sfx(2, 0, 13, 2, 0); break;                /* noise */
    case STE_LOCK: ay_sfx(1, P(220), 12, 2, 24); break;
    case STE_HOLD: ay_sfx(1, P(1300), 11, 2, 6); break;
    case STE_GARBAGE: ay_sfx(2, 0, 13, 1, 0); break;
    case STE_CLEAR: play_jingle(j_clear); break;
    case STE_LEVEL: play_jingle(j_level); break;
    case STE_OVER: ay_sfx(0, P(440), 14, 1, 40); break;
    case STE_WIN: play_jingle(j_win); break;
    }
}

void snd_tick(void)
{
    ZXN_WRITE_REG(0x50, st_song_page);
    modplay_tick();                     /* leaves chip 0 selected */
    ZXN_WRITE_REG(0x50, 0xFF);
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
    if ((snd_reg(1, 8) | snd_reg(1, 9) | snd_reg(1, 10)) & 15) snd_sounding[1]++;
    if (snd_reg(2, 8) & 15) snd_sounding[2]++;
}

/* what an AY chip plays now (the test checks music and effects through
   this): register r of chip c, read back through port 0xFFFD */
uint8_t snd_reg(uint8_t c, uint8_t r)
{
    uint8_t v;
    z80_outp(0xFFFD, 0xFF - c);
    z80_outp(0xFFFD, r);
    v = z80_inp(0xFFFD);
    z80_outp(0xFFFD, 0xFF);
    return v;
}
