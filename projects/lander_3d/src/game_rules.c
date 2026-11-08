/*
 * game_rules.c - the parts of Lander 3D's game shell that work with the
 * rules (game.c's tick, the original's outcome(), the HUD's values, the
 * self-test), in the rules' bank (20) with them: resident memory is full.
 */
#include <string.h>
#include <stdio.h>
#include "zxlog.h"
#include "game.h"
#include "game_rules.h"
#include "sound.h"
#include "view.h"
#include "flight_hash.h"

static uint8_t ap_wait;                  /* steps until the autopilot decides again */
static LdInput ap_in;

void rules_autopilot_reset(void) { ap_wait = 0; }

static void pilot_input(LdInput *in, uint8_t held)
{
    in->throttle = (held & IN_FIRE) ? FONE : lander.steady;
    in->pitch = (held & IN_UP) ? FONE : (held & IN_DOWN) ? -FONE : 0;
    in->roll = (held & IN_LEFT) ? FONE : (held & IN_RIGHT) ? -FONE : 0;
}

/* a tick's four steps (the autopilot's or the pilot's), the events'
   sounds; 1 when the demo should start its next level, 2 at a landing or
   a crash (game.c then calls game_outcome()) */
uint8_t rules_tick(uint8_t held)
{
    uint8_t k, again = 0;
    int e;
    view_tick(&level);
    for (k = 0; k < LD_STEP_HZ / 25; k++) {
        LdInput li;
        if (lander.demo) {
            if (!ap_wait) { ld_autopilot(&level, &ap_in); ap_wait = 16; }
            ap_wait--;
            li.throttle = ap_in.throttle;            /* (field by field: zsdcc's struct copies) */
            li.pitch = ap_in.pitch;
            li.roll = ap_in.roll;
        } else pilot_input(&li, held);
        ld_step(&level, &li);
    }
    while ((e = ld_pop_event(&level)) != LDE_NONE)
        if (e == LDE_FUEL_OUT && lander.state == LS_PLAY) { snd_play(SFX_FUEL); zx_log("ld: fuel out\n"); }
    if (lander.state == LS_PLAY && !level.outcome) {
        if (level.c.throttle > FX(0.05) && (lander.frame % 6) == 0) snd_play(SFX_THRUST);
        if ((level.c.rcs[0] || level.c.rcs[1]) && (lander.frame % 5) == 0) snd_play(SFX_RCS);
    }
    if (level.outcome) {
        if (lander.state == LS_PLAY) again = 2;      /* game.c: game_outcome() */
        else if (lander.frame % 75 == 0) again = 1;
    }
    ter_done();
    return again;
}
/* ---------------------------------------------------------------- the HUD's values */
void hud_values(void)
{
    const Pad *p;
    fix t;
    uint8_t k;
    k = (uint8_t)ld_nearest_pad(&level, 0);
    p = &level.T.pads[k];
    hv.agl = ld_agl(&level);
    hv.vs = -level.c.v[1];
    hv.hs = fhypot(level.c.v[0], level.c.v[2]);
    hv.tilt = ld_tilt(&level.c);
    t = level.fuel_max ? fdiv(level.fuel, level.fuel_max) : 0;
    hv.fuel = (uint8_t)((t * 100) >> 16);
    hv.thr = (uint8_t)((level.c.throttle * 100) >> 16);
    hv.pad = k;
    hv.pad_dist = FINT(fhypot(p->x - level.c.p[0], p->z - level.c.p[2]));
    hv.pad_dir = a3_atan2((p->x - level.c.p[0]) >> 4, (p->z - level.c.p[2]) >> 4);
    hv.wind_dir = a3_atan2(level.wind[0] >> 4, level.wind[1] >> 4);
    hv.wind = level.params.wind != 0;
    hv.lim_vs = ld_worlds[level.world].lim_vs;
    hv.lim_hs = ld_worlds[level.world].lim_hs;
    hv.lim_tilt = ld_worlds[level.world].lim_tilt;
    ter_done();
}

/* the selftest (T held at start-up): flights of flight_hash.h, their hashes
   against the original's (test.py, build/levels/expect.txt) */
void selftest(void)
{
    static const uint8_t f[3][3] = { { 0, 0, 1 }, { 1, 1, 5 }, { 1, 2, 10 } };   /* kind, world, level */
    uint8_t k;
    for (k = 0; k < 3; k++) {
        int32_t steps;
        uint32_t h;
        ld_new_level(&level, f[k][1], f[k][2]);
        h = flight_hash_kind(&level, f[k][0], FLIGHT_STEPS, &steps);
        ter_done();
        zx_log("ld: selftest flight %u %u %u %ld %d %ld %08lX\n", f[k][0], f[k][1], f[k][2], steps, level.outcome,
               level.result.score, h);
    }
}

