/*
 * Orbital Patrol's sound (the original's sound.c played ptplayer and
 * sampled effects): the MOD converted by sdk/tools/mod2ay.py plays on
 * TurboSound chips 1 and 2; the effects are AY effects on chip 0, shaped
 * like the original's synthesised samples (a falling laser pulse, noise
 * bursts, a rising arpeggio...). Every AY write happens in the frame
 * interrupt: the game only queues the effects.
 */
#include <stdint.h>
#include "zxgame.h"
#include "nextgfx.h"
#include "modplay.h"
#include "sound.h"

extern const mod_song op_song;           /* music.c */
static volatile uint16_t pending;        /* effects to start, bit per effect */
enum { S_LASER, S_EXPLODE, S_EXPLODE_BIG, S_PICKUP, S_HUMAN_DIE, S_BOMB, S_HYPER, S_DIE, S_LEVEL };

void sound_init(void)
{
    ay_init();
    modplay_start(&op_song);             /* the original plays its MOD from start-up on */
}

void sound_events(GameState *gs)
{
    uint16_t p = 0;
    if (gs->ev_laser) p |= 1 << S_LASER;
    if (gs->ev_explode) p |= 1 << S_EXPLODE;
    if (gs->ev_explode_big) p |= 1 << S_EXPLODE_BIG;
    if (gs->ev_pickup) p |= 1 << S_PICKUP;
    if (gs->ev_human_die) p |= 1 << S_HUMAN_DIE;
    if (gs->ev_bomb) p |= 1 << S_BOMB;
    if (gs->ev_hyper) p |= 1 << S_HYPER;
    if (gs->ev_die) p |= 1 << S_DIE;
    if (gs->ev_level) p |= 1 << S_LEVEL;
    gs->ev_laser = gs->ev_explode = gs->ev_explode_big = gs->ev_pickup = gs->ev_human_die = 0;
    gs->ev_bomb = gs->ev_hyper = gs->ev_die = gs->ev_level = 0;
    pending |= p;
}

void sound_frame(void)
{
    uint16_t p = pending;
    pending = 0;
    modplay_tick();                      /* leaves chip 0 selected */
    /* chip 0: A the laser and tones, B noise, C the longer effects */
    if (p & (1 << S_LASER)) ay_sfx(0, 70, 13, 2, 14);          /* a fast falling pulse */
    if (p & (1 << S_EXPLODE)) ay_sfx(1, 0, 14, 1, 0);          /* a noise burst */
    if (p & (1 << S_EXPLODE_BIG)) { ay_sfx(1, 0, 15, 1, 0); ay_sfx(2, 1800, 15, 1, 60); }
    if (p & (1 << S_PICKUP)) ay_sfx(2, 280, 14, 1, -18);       /* rising */
    if (p & (1 << S_HUMAN_DIE)) ay_sfx(2, 200, 14, 1, 40);     /* a falling buzz */
    if (p & (1 << S_BOMB)) { ay_sfx(1, 0, 15, 1, 0); ay_sfx(2, 1200, 15, 1, -40); }
    if (p & (1 << S_HYPER)) ay_sfx(2, 500, 14, 1, -30);        /* a sweep */
    if (p & (1 << S_DIE)) { ay_sfx(1, 0, 15, 1, 0); ay_sfx(2, 300, 15, 1, 50); }
    if (p & (1 << S_LEVEL)) ay_sfx(2, 340, 15, 1, -12);
    sound_tick();
}
