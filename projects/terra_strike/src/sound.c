/*
 * Terra Strike - sound: the original's effects on TurboSound chip 0 (the
 * original synthesises gunfire, explosions, hits and the crash on Paula).
 * The MOD comes later (tools/make_music).
 */
#include <stdint.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "ts.h"
#include "sound.h"

void snd_init(void)
{
    ay_init();
    ay_chip(0);
}

void snd_event(int16_t ev)
{
    ay_chip(0);
    switch (ev) {
    case EV_FIRE:       ay_sfx(0, 120, 12, 3, 6); break;
    case EV_KILL:       ay_noise(1, 24, 15); ay_sfx(2, 900, 15, 1, 20); break;
    case EV_LEVEL_CLEAR: ay_noise(1, 24, 15); break;
    case EV_HIT_PLAYER: ay_sfx(2, 300, 15, 2, -10); break;
    case EV_CRASH:      ay_noise(1, 30, 15); ay_sfx(2, 1200, 15, 1, 30); break;
    case EV_ENEMY_FIRE: ay_sfx(0, 500, 9, 3, 12); break;
    case EV_TAKEOFF:    ay_sfx(2, 700, 10, 1, -20); break;
    case EV_LANDED:     ay_sfx(2, 400, 10, 1, 10); break;
    case EV_LOW_FUEL:   ay_sfx(2, 150, 13, 1, 0); break;
    }
}

void snd_music(uint8_t on)
{
    (void)on;
}

void snd_tick(uint8_t frames)
{
    ay_chip(0);
    while (frames--) sound_tick();
}
