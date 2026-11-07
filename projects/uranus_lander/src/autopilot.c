/*
 * Uranus Lander - autopilot.
 *
 * port: the Atari ST port's autopilot (atari_st_games/uranus_lander/
 * autopilot.py), which flew the game from the host through the emulator's
 * API, moved into the game in fixed point: Z switches it on and off while
 * playing, and the test's bot is this. Same rules: aim for the best
 * multiplier pad (then the nearest), a wanted sideways speed towards its
 * centre reached by tilting, a descent speed limit that shrinks with
 * altitude, hover with clearance until over the pad, upright for touchdown.
 * Speeds here are Q8 pixels per frame (the 16.16 velocities >> 8).
 */
#include "game.h"
#include "autopilot.h"

static const LandingPad *target;
static WORD target_level = -1;
static uint8_t hold_left, hold_right, hold_thrust, tick;

static WORD clampw(WORD v, WORD lo, WORD hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static WORD absw(WORD v) { return v < 0 ? -v : v; }

/* best multiplier, then nearest (port: by index: zsdcc got the pointer
   version wrong) */
static uint8_t choose_pad(const GameState *gs, WORD x)
{
    uint8_t i, best = 0;
    WORD best_m = -1, best_d = 0;
    for (i = 0; i < (uint8_t)gs->num_pads; i++) {
        WORD m = gs->pads[i].multiplier;
        WORD d = absw(gs->pads[i].x + gs->pads[i].width / 2 - x);
        if (m > best_m || (m == best_m && d < best_d)) {
            best = i;
            best_m = m;
            best_d = d;
        }
    }
    return best;
}

void autopilot_reset(void)
{
    target_level = -1;
    hold_left = hold_right = hold_thrust = 0;
    tick = 0;
}

void autopilot_fly(const GameState *gs, InputState *in)
{
    const Ship *s = &gs->ship;
    WORD x = (WORD)(s->x >> 16), y = (WORD)(s->y >> 16);
    WORD vx = (WORD)(s->vx >> 8), vy = (WORD)(s->vy >> 8);     /* Q8 */
    WORD angle = s->angle < 128 ? s->angle : s->angle - 256;   /* signed, 0 = up */

    if (target_level != gs->level) {
        target = &gs->pads[choose_pad(gs, x)];
        target_level = gs->level;
    }
    /* like the original bot, decide every second frame and hold in between */
    if ((tick++ & 1) == 0) {
        WORD cx = target->x + target->width / 2;
        WORD err = clampw(cx - x, -40, 40);
        uint8_t over_pad = absw(cx - x) < target->width / 2 - 3;
        WORD ground = gs->terrain_y[clampw(x, 0, TERRAIN_W - 1)];
        WORD alt, vx_want, dv, angle_want, vy_want;
        if (target->y < ground) ground = target->y;
        alt = ground - (y + 6);             /* the ship's bottom is y + 6 */

        vx_want = clampw((WORD)(((LONG)err * 205) >> 5), -230, 230);   /* err * 0.025, |0.9| */
        dv = vx_want - vx;
        if (alt < 18 && over_pad)
            angle_want = 0;                 /* upright for touchdown */
        else
            angle_want = clampw((WORD)(((LONG)dv * 40 + 128) >> 8), -24, 24);

        vy_want = clampw(64 + alt * 3, 64, 358);              /* 0.25 + alt * 0.012, 0.25..1.4 */
        if (!over_pad) {
            WORD keep = (alt - 35) * 5;                       /* (alt - 35) * 0.02: ~35 px clearance */
            if (keep < vy_want) vy_want = keep;
        }
        hold_thrust = vy > vy_want;
        if (absw(dv) > 38 && absw(angle) > 4 && vy > -102 && alt > 20)
            hold_thrust = 1;                /* use tilted thrust to move sideways */
        if (vy < -154)
            hold_thrust = 0;

        hold_left = angle > angle_want + 1;
        hold_right = angle < angle_want - 1;
    }
    in->left = hold_left;
    in->right = hold_right;
    in->thrust = hold_thrust;
    in->up = 0;
}
