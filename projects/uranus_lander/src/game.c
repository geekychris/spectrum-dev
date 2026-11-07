/*
 * Uranus Lander - game logic (port of amiga_mcp/examples/uranus_lander/game.c).
 *
 * The rules, terrain generator, pads, physics, landing test and scoring are
 * the original's, in its 320x256 world with 16.16 fixed point. Port changes
 * are marked "port:".
 */
#include <string.h>
#include "game.h"
#include "fixmath.h"
#include "zxgame.h"

Tunables g_tune = {
    GRAVITY_DEFAULT,
    THRUST_DEFAULT,
    FUEL_DEFAULT,
    SAFE_VY_DEFAULT,
    SAFE_VX_DEFAULT,
    SAFE_ANGLE_DEFAULT,
    TURN_RATE_DEFAULT,
    START_LIVES_DEFAULT
};

/* RNG: the original's LCG, so every level's terrain is the original's */
static ULONG rng_state = 12345;
static WORD rng_next(void)
{
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (WORD)((rng_state >> 16) & 0x7FFF);
}

/* Generate stars with parallax speeds */
static void generate_stars(GameState *gs)
{
    WORD i;
    ULONG seed = 777777;
    Star *s = gs->stars;
    for (i = 0; i < MAX_STARS; i++, s++) {
        seed = seed * 1103515245UL + 12345UL;
        s->x = (WORD)((seed >> 16) % SCREEN_W);
        s->y = (WORD)((seed >> 3) % (SCREEN_H - 30));
        s->accum = 0;
        if (i < 15) {
            s->brightness = COL_WHITE;
            s->speed = 3; /* bright = fastest parallax layer */
        } else if (i < 35) {
            s->brightness = COL_LTGRAY;
            s->speed = 2; /* medium */
        } else {
            s->brightness = COL_SLATE;
            s->speed = 1; /* dim = slowest */
        }
    }
}

/* Scroll stars leftward with sub-pixel accumulation.
 * Stars only ever move 0 or 1 pixel per frame - no multi-pixel jumps.
 * port: in assembly (stars.asm; in C it was a quarter of the frame), and n
 * frames at once (the main loop's catch-up ticks): the same as n calls */
extern void stars_update_asm(Star *stars, uint8_t n);
void stars_update(GameState *gs, uint8_t n)
{
    if (n) stars_update_asm(gs->stars, n);
}

/* Width based on multiplier */
static WORD pad_width(WORD mult)
{
    switch (mult) {
        case 1: return 40;
        case 2: return 30;
        case 3: return 22;
        default: return 14; /* x5 */
    }
}

/* Multipliers get harder as level increases */
static const WORD mult_table[4][4] = {
    {1, 1, 2, 2},   /* level 1-2 */
    {1, 2, 2, 3},   /* level 3-4 */
    {2, 2, 3, 5},   /* level 5-6 */
    {2, 3, 5, 5},   /* level 7+ */
};

/* Generate terrain for current level */
static void generate_terrain(GameState *gs)
{
    WORD x, i;
    WORD y = 200;
    WORD roughness = 2 + gs->level;
    WORD *t;

    if (roughness > 8) roughness = 8;

    /* Seed RNG based on level */
    rng_state = (ULONG)((LONG)gs->level * 31337L + 54321L);   /* port: in 32 bits, as the
        68k's int (z88dk's int is 16 bits) */

    /* Generate random terrain */
    t = gs->terrain_y;
    for (x = 0; x < TERRAIN_W; x++) {
        WORD delta = (rng_next() % (roughness * 2 + 1)) - roughness;
        y += delta;
        if (y < TERRAIN_MIN) y = TERRAIN_MIN + 5;
        if (y > TERRAIN_MAX) y = TERRAIN_MAX - 5;
        *t++ = y;
    }

    /* Place landing pads */
    gs->num_pads = 0;
    {
        WORD num_zones = 4;
        WORD zone_w = TERRAIN_W / num_zones;
        WORD diff = (gs->level - 1) / 2;
        LandingPad *pd = gs->pads;
        if (diff > 3) diff = 3;

        for (i = 0; i < num_zones && gs->num_pads < MAX_PADS; i++, pd++) {
            WORD mult = mult_table[diff][i];
            WORD width = pad_width(mult);
            WORD zone_start = i * zone_w + 10;
            WORD pad_x, pad_y, px;

            /* Pick X position within zone */
            pad_x = zone_start + (rng_next() % (zone_w - width - 20));
            if (pad_x < 5) pad_x = 5;
            if (pad_x + width >= TERRAIN_W - 5) pad_x = TERRAIN_W - width - 5;

            /* Use average height at pad location */
            pad_y = gs->terrain_y[pad_x + width / 2];
            if (pad_y < TERRAIN_MIN + 10) pad_y = TERRAIN_MIN + 10;
            if (pad_y > TERRAIN_MAX - 5) pad_y = TERRAIN_MAX - 5;

            /* Flatten terrain at pad */
            for (px = pad_x; px < pad_x + width && px < TERRAIN_W; px++)
                gs->terrain_y[px] = pad_y;

            /* Smooth transitions at pad edges */
            if (pad_x > 2) {
                WORD diff_l = gs->terrain_y[pad_x - 1] - pad_y;
                if (diff_l > 3 || diff_l < -3)
                    gs->terrain_y[pad_x - 1] = pad_y + diff_l / 2;
            }
            if (pad_x + width < TERRAIN_W - 2) {
                WORD diff_r = gs->terrain_y[pad_x + width] - pad_y;
                if (diff_r > 3 || diff_r < -3)
                    gs->terrain_y[pad_x + width] = pad_y + diff_r / 2;
            }

            pd->x = pad_x;
            pd->width = width;
            pd->y = pad_y;
            pd->multiplier = mult;
            gs->num_pads++;
        }
    }
}

/* Reset ship to starting position */
static void reset_ship(GameState *gs)
{
    gs->ship.x = FIX16(SCREEN_W / 2);
    gs->ship.y = FIX16(20);
    gs->ship.vx = 0;
    gs->ship.vy = 0;
    gs->ship.angle = 0;
    gs->ship.fuel = (WORD)g_tune.fuel_max;
    gs->ship.alive = 1;
    gs->ship.thrusting = 0;
}

/* Spawn explosion particles */
static void spawn_explosion(GameState *gs, Fixed x, Fixed y)
{
    WORD i;
    Particle *p = gs->particles;
    for (i = 0; i < MAX_PARTICLES; i++, p++) {
        WORD r;
        if (p->active) continue;
        p->x = x;
        p->y = y;
        r = rng_next() % 16000 - 8000;       /* port: one call per statement (zsdcc) */
        p->vx = (Fixed)r << 2;
        r = rng_next() % 16000 - 12000;
        p->vy = (Fixed)r << 2;
        p->life = 20 + (rng_next() % 30);
        if ((rng_next() & 3) == 0) p->color = COL_RED;
        else p->color = (rng_next() & 1) ? COL_ORANGE : COL_YELLOW;
        p->active = 1;
        gs->particles_live++;
    }
}

/* port: the loop (x += vx, y += vy, vy += 600 particle gravity, life--,
   off at 0) is particles.asm: in zsdcc's 32-bit C it took more than a frame */
extern void particles_update_asm(Particle *p, WORD *live);
static void update_particles(GameState *gs)
{
    if (gs->particles_live)            /* port: nothing to do (same result, faster) */
        particles_update_asm(gs->particles, &gs->particles_live);
}

/* port: the SDK's table (5 entries) */
WORD hiscore_qualifies(GameState *gs)
{
    return hs_rank((uint32_t)gs->score) >= 0;
}

void game_init(GameState *gs)
{
    memset(gs, 0, sizeof(GameState));

    /* Restore level-dependent physics defaults so a fresh game does not
     * inherit the harder tune left over from previous game_new_level runs. */
    g_tune.gravity  = GRAVITY_DEFAULT;
    g_tune.fuel_max = FUEL_DEFAULT;

    gs->lives = (WORD)g_tune.start_lives;
    gs->level = 1;
    gs->score = 0;
    gs->state = STATE_PLAYING;

    generate_stars(gs);
    generate_terrain(gs);
    reset_ship(gs);
}

void game_new_level(GameState *gs)
{
    WORD i;
    gs->level++;

    /* Increase difficulty */
    g_tune.gravity = GRAVITY_DEFAULT + (gs->level - 1) * 150L;
    g_tune.fuel_max = FUEL_DEFAULT - (gs->level - 1) * 30L;
    if (g_tune.fuel_max < 200) g_tune.fuel_max = 200;

    for (i = 0; i < MAX_PARTICLES; i++)
        gs->particles[i].active = 0;
    gs->particles_live = 0;

    generate_terrain(gs);
    reset_ship(gs);
}

static void crash(GameState *gs, WORD timer)
{
    gs->ship.alive = 0;
    gs->state = STATE_CRASHING;
    gs->state_timer = timer;
    gs->ev_crash = 1;
}

/* Check landing on terrain/pad */
static void check_collision(GameState *gs)
{
    WORD sx = FIX_ROUND(gs->ship.x);
    WORD sy = FIX_ROUND(gs->ship.y);
    WORD ship_bottom = sy + 6; /* ship extends ~6px below center */
    WORD terrain_h;
    WORD i, pad_idx;
    Fixed abs_vx, abs_vy;
    WORD angle;
    LandingPad *pd;

    if (sx < 0 || sx >= TERRAIN_W) return;

    terrain_h = gs->terrain_y[sx];

    if (ship_bottom < terrain_h) return; /* still airborne */

    /* Hit the ground - check if on a pad */
    pad_idx = -1;
    pd = gs->pads;
    for (i = 0; i < gs->num_pads; i++, pd++) {
        if (sx >= pd->x && sx < pd->x + pd->width) {
            pad_idx = i;
            break;
        }
    }

    if (pad_idx < 0) {
        /* Hit rough terrain - crash! */
        crash(gs, 80);
        spawn_explosion(gs, gs->ship.x, gs->ship.y);
        return;
    }

    /* On a pad - check landing conditions */
    abs_vx = gs->ship.vx < 0 ? -gs->ship.vx : gs->ship.vx;
    abs_vy = gs->ship.vy < 0 ? -gs->ship.vy : gs->ship.vy;
    angle = gs->ship.angle;
    if (angle > 128) angle = 256 - angle;

    if (gs->ship.vy < 0 ||
        abs_vy > g_tune.safe_vy ||
        abs_vx > g_tune.safe_vx ||
        angle > g_tune.safe_angle) {
        /* Bad landing - crash! */
        crash(gs, 80);
        spawn_explosion(gs, gs->ship.x, gs->ship.y);
        return;
    }

    /* Successful landing! */
    gs->landed_pad = pad_idx;
    gs->land_bonus = (LONG)pd->multiplier * 50L + (LONG)gs->ship.fuel;
    gs->score += gs->land_bonus;
    gs->ship.vx = 0;
    gs->ship.vy = 0;
    gs->ship.thrusting = 0;
    gs->state = STATE_LANDED;
    gs->state_timer = 100;
    gs->ev_land = 1;
}

void game_update(GameState *gs, InputState *inp)
{
    gs->ev_thrust = 0;
    gs->ev_crash = 0;
    gs->ev_land = 0;
    gs->ev_low_fuel = 0;

    switch (gs->state) {
    case STATE_PLAYING:
    {
        Ship *s = &gs->ship;

        /* Rotation */
        if (inp->left)  s->angle = (s->angle - (WORD)g_tune.turn_rate) & 255;
        if (inp->right) s->angle = (s->angle + (WORD)g_tune.turn_rate) & 255;

        /* Thrust */
        s->thrusting = 0;
        if ((inp->thrust || inp->up) && s->fuel > 0) {
            /* port: Q14 sine, the 16 x 16 multiply of fixmath.h */
            int16_t t = (int16_t)g_tune.thrust;
            Fixed thrust_x = mul16(fix_sin14(s->angle), t) >> 14;
            Fixed thrust_y = -(mul16(fix_cos14(s->angle), t) >> 14);
            s->vx += thrust_x;
            s->vy += thrust_y;
            s->fuel--;
            s->thrusting = 1;
            gs->ev_thrust = 1;
        }

        /* Gravity (always pulls down = positive Y) */
        s->vy += (Fixed)g_tune.gravity;

        s->x += s->vx;
        s->y += s->vy;

        /* Clamp X to screen bounds */
        if (s->x < FIX16(5)) { s->x = FIX16(5); s->vx = 0; }
        if (s->x > FIX16(SCREEN_W - 5)) { s->x = FIX16(SCREEN_W - 5); s->vx = 0; }

        /* If ship goes too high, let it (but clamp for sanity) */
        if (s->y < FIX16(-40)) s->y = FIX16(-40);

        /* If ship goes below screen, crash */
        {
            WORD sy = FIX_ROUND(s->y);      /* port: through a WORD (zsdcc) */
            if (sy > SCREEN_H)
                crash(gs, 60);
        }

        if (s->alive)
            check_collision(gs);

        /* Low fuel warning */
        if (s->fuel > 0 && s->fuel < 80 && (gs->frame & 31) == 0)
            gs->ev_low_fuel = 1;

        /* Count down the post-respawn invulnerability grace period so
         * draw_ship stops blinking after ~50 ticks. */
        if (gs->state_timer > 0)
            gs->state_timer--;
        break;
    }

    case STATE_LANDED:
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            game_new_level(gs);
            gs->state = STATE_PLAYING;
        }
        break;

    case STATE_CRASHING:
        update_particles(gs);
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            gs->lives--;
            if (gs->lives <= 0) {
                gs->state = STATE_GAMEOVER;
                gs->state_timer = 60; /* delay before accepting input */
            } else {
                reset_ship(gs);
                gs->state = STATE_PLAYING;
                gs->state_timer = 50; /* brief invulnerability */
            }
        }
        break;

    case STATE_GAMEOVER:
        update_particles(gs);
        if (gs->state_timer > 0) {
            gs->state_timer--;
        } else if (inp->thrust || inp->up) {
            /* port: main.c runs the SDK's initials editor (hs_enter) */
            gs->state = hiscore_qualifies(gs) ? STATE_ENTER_NAME : STATE_TITLE;
        }
        break;

    default:
        break;
    }

    /* Always update particles */
    if (gs->state == STATE_PLAYING || gs->state == STATE_LANDED)
        update_particles(gs);

    gs->frame++;
}
