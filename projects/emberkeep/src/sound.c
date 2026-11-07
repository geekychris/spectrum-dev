/*
 * Emberkeep - sound. port: the original's audio.cpp is a stub that only
 * logs (its README lists sound as follow-up work), so the effects and the
 * two tunes are the port's: TurboSound chip 0 through the SDK, the tunes on
 * channel C, the effects on A and B.
 */
#include <stdint.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "sound.h"


/* the tunes as ay_note lists, made at start-up from MIDI notes */
static ay_note title_tune[17];
static ay_note deep_tune[9];

static void make_tunes(void)
{
    /* A minor, slow: A C E A G E D E */
    static const uint8_t t[16][2] = {
        {57, 20}, {60, 20}, {64, 20}, {69, 40}, {67, 20}, {64, 20}, {62, 20}, {64, 40},
        {57, 20}, {60, 20}, {65, 20}, {69, 40}, {67, 20}, {65, 20}, {64, 20}, {57, 60},
    };
    static const uint8_t d[8][2] = {
        {33, 75}, {0, 25}, {36, 75}, {0, 25}, {31, 75}, {0, 25}, {28, 100}, {0, 50},
    };
    uint8_t i;
    for (i = 0; i < 16; i++) {
        title_tune[i].period = AY_NOTE(t[i][0]);
        title_tune[i].len = t[i][1];
    }
    title_tune[16].period = 0;
    title_tune[16].len = 0;
    for (i = 0; i < 8; i++) {
        deep_tune[i].period = d[i][0] ? AY_NOTE(d[i][0]) : 0;
        deep_tune[i].len = d[i][1];
    }
    deep_tune[8].period = 0;
    deep_tune[8].len = 0;
}

void snd_init(void)
{
    ay_init();
    ay_chip(0);
    make_tunes();
}

void snd_play(uint8_t s)
{
    switch (s) {
    case SND_RUNE:  ay_sfx(0, AY_NOTE(76), 15, 1, -4); ay_sfx(1, AY_NOTE(83), 12, 1, -2); break;
    case SND_OIL:   ay_noise(1, 6, 12); ay_sfx(0, AY_NOTE(55), 13, 1, -12); break;
    case SND_DEATH: ay_sfx(0, AY_NOTE(50), 15, 1, 24); ay_noise(1, 20, 14); break;
    case SND_EXIT:  ay_sfx(0, AY_NOTE(64), 15, 1, -10); ay_sfx(1, AY_NOTE(71), 13, 1, -8); break;
    case SND_LOW:   ay_sfx(1, AY_NOTE(88), 9, 2, 0); break;
    case SND_OVER:  ay_sfx(0, AY_NOTE(45), 15, 1, 30); break;
    case SND_WIN:   ay_sfx(0, AY_NOTE(69), 15, 1, -12); ay_sfx(1, AY_NOTE(76), 15, 1, -12); break;
    }
}

void snd_music(uint8_t m)
{
    if (m == MUSIC_TITLE) music_play(2, title_tune, 10);
    else if (m == MUSIC_DEEP) music_play(2, deep_tune, 7);
    else music_stop();
}

void snd_tick(void)
{
    sound_tick();
}
