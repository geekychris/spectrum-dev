/*
 * Rock Blaster - game logic (port of amiga_mcp/examples/rock_blaster/game.c).
 *
 * The rules, waves, scoring, speeds and the order of the update are the
 * original's. Changes are marked "port:"; most are about doing the same sums
 * in 16 bits on a Z80.
 */
#include <stdint.h>
#include "game.h"
#include "zxgame.h"
#include "fixmath.h"
#include "hit48.h"

const uint8_t rock_radius_tab[3] = {20, 12, 6};

static uint8_t hits_valid;              /* the rocks' collision list is up to date */

/* port: the SDK's xorshift (seeded by main) instead of the 32-bit LCG,
   whose multiply is slow on a Z80; same 15-bit range */
static uint16_t rng(void)
{
    return rnd16() & 0x7FFF;
}

static int16_t rng_range(int16_t lo, int16_t hi)
{
    return lo + (int16_t)(mul16((int16_t)rng(), hi - lo) >> 15);
}

#define PX7(v) ((int16_t)((v) >> 7))

/* port: (v * 15) / 16 with C's truncation, without a 32-bit multiply and
   divide; the drag, (v * 253) / 256, is in objs.asm (ship_move) */
static Fixed slow15(Fixed v)
{
    if (v >= 0) return v - ((v + 15) >> 4);
    v = -v;
    return -(v - ((v + 15) >> 4));
}

static void ship_reset(Ship *s)
{
    s->x = FX16(WORLD_W / 2);
    s->y = FX16(WORLD_H / 2);
    s->dx = 0;
    s->dy = 0;
    s->alive = 1;
    s->invuln_timer = SHIP_INVULN_TIME;
    s->px = WORLD_W / 2;
    s->py = WORLD_H / 2;
}

void game_init(GameState *gs)
{
    uint8_t i;
    gs->score = 0;
    gs->lives = TUNE_START_LIVES;
    gs->level = 1;
    gs->state = STATE_PLAYING;
    gs->state_timer = 0;
    gs->rock_count = 0;
    gs->frame = 0;

    ship_reset(&gs->ship);
    gs->ship.angle = 192;            /* pointing up */
    gs->ship.thrust_on = 0;

    for (i = 0; i < MAX_BULLETS; i++) gs->bullets[i].active = 0;
    for (i = 0; i < MAX_ROCKS; i++) gs->rocks[i].active = 0;
    for (i = 0; i < MAX_PARTICLES; i++) gs->particles[i].active = 0;
    particles_live = 0;

    game_spawn_rocks(gs, TUNE_START_ROCKS);
}

/* rock speeds in 1/128 px a frame: the tunable 0.80 px, x1.5 medium, x2 small */
#define ROCK_BASE ((int16_t)(TUNE_ROCK_SPEED * 128L / 100))

static void rock_launch(Rock *r, int16_t base_speed)
{
    uint8_t a = (uint8_t)rng();
    int16_t spd = rng_range(base_speed / 2, base_speed);
    r->dx = mulq8(fix_cos(a), spd);
    r->dy = mulq8(fix_sin(a), spd);
    r->shape = (uint8_t)(rng() & 3);
    /* port: the original also drew a visual angle and spin here, which its
       drawing never used */
    r->active = 1;
    r->px = PX7(r->x);
    r->py = PX7(r->y);
}

void game_spawn_rocks(GameState *gs, int16_t count)
{
    int16_t j;
    for (j = 0; j < count && gs->rock_count < MAX_ROCKS; j++) {
        Rock *r = gs->rocks;
        uint8_t i;
        for (i = 0; i < MAX_ROCKS; i++, r++) {
            if (!r->active) {
                switch ((uint8_t)(rng() & 3)) {          /* spawn at an edge */
                case 0: r->x = 0;  r->y = (pos7)rng_range(0, (int16_t)(H7 / 2)) << 1; break;
                case 1: r->x = W7; r->y = (pos7)rng_range(0, (int16_t)(H7 / 2)) << 1; break;
                case 2: r->x = (pos7)rng_range(0, (int16_t)(W7 / 2)) << 1; r->y = 0; break;
                default: r->x = (pos7)rng_range(0, (int16_t)(W7 / 2)) << 1; r->y = H7; break;
                }
                r->size = ROCK_LARGE;
                rock_launch(r, ROCK_BASE);
                hits_valid = 0;
                gs->rock_count++;
                break;
            }
        }
    }
}

/* two smaller rocks where a rock was destroyed */
static void split_rock(GameState *gs, Rock *parent)
{
    uint8_t new_size, j, i;
    int16_t base_speed = ROCK_BASE;

    if (parent->size == ROCK_SMALL) return;
    new_size = parent->size + 1;
    if (new_size == ROCK_MEDIUM) base_speed = (base_speed * 3) / 2;
    if (new_size == ROCK_SMALL) base_speed = base_speed * 2;

    for (j = 0; j < 2 && gs->rock_count < MAX_ROCKS; j++) {
        Rock *r = gs->rocks;
        for (i = 0; i < MAX_ROCKS; i++, r++) {
            if (!r->active) {
                r->x = parent->x;
                r->y = parent->y;
                r->size = new_size;
                rock_launch(r, base_speed);
                gs->rock_count++;
                break;
            }
        }
    }
}

void game_spawn_particles(GameState *gs, pos7 x, pos7 y, uint8_t count)
{
    uint8_t j, i;
    for (j = 0; j < count; j++) {
        Particle *p = gs->particles;
        for (i = 0; i < MAX_PARTICLES; i++, p++) {
            if (!p->active) {
                uint8_t a = (uint8_t)rng();
                int16_t spd = rng_range(0, V7(3));
                p->x = x;
                p->y = y;
                p->dx = mulq8(fix_cos(a), spd);
                p->dy = mulq8(fix_sin(a), spd);
                p->life = 10 + (uint8_t)(rng() % 20);
                particles_live++;
                /* port: no colour (1-bit screen) */
                p->active = 1;
                break;
            }
        }
    }
}

/* objs.asm: the movement loops */
extern void *mw_ptr, *pm_ptr;
extern uint8_t mw_n, pm_n, pm_live;
uint8_t particles_live;                 /* active particles (main.c skips none) */
extern uint16_t mw_stride;
extern void mw_core(void);
extern void pm_core(void);
extern void ship_move(Ship *s) __z88dk_fastcall;
extern hit_circle *mw_hits;

/* the rocks as circles for the collision tests (hit48), in slot order like
   the original's loops; rebuilt after every hit, since a hit changes them */
static hit_circle rock_hits[MAX_ROCKS];
static uint8_t n_rock_hits;

extern const Rock *bh_rocks;
extern hit_circle *bh_out;
extern uint8_t bh_n;
extern void bh_core(void);

static void build_rock_hits(GameState *gs)
{
    bh_rocks = gs->rocks;
    bh_out = rock_hits;
    bh_n = 0;
    bh_core();
    n_rock_hits = bh_n;
}

#define BULLET_SPD ((int16_t)(TUNE_BULLET_SPEED * 128L / 100))
#define THRUST_8   ((int16_t)((TUNE_SHIP_THRUST * 65536L / 100) >> 8))

static void fire_bullet(GameState *gs)
{
    Bullet *b = gs->bullets;
    uint8_t i;
    for (i = 0; i < MAX_BULLETS; i++, b++) {
        if (!b->active) {
            b->x = (pos7)(gs->ship.x >> 9);
            b->y = (pos7)(gs->ship.y >> 9);
            b->dx = mulq8(fix_cos(gs->ship.angle), BULLET_SPD);
            b->dy = mulq8(fix_sin(gs->ship.angle), BULLET_SPD);
            b->life = BULLET_LIFE;
            b->active = 1;
            b->px = PX7(b->x);
            b->py = PX7(b->y);
            break;
        }
    }
}

static void ship_controls(GameState *gs, uint8_t joy_left, uint8_t joy_right,
                          uint8_t joy_up, uint8_t joy_fire)
{
    Ship *s = &gs->ship;
    if (s->alive) {
        if (joy_left) s->angle -= SHIP_TURN_SPEED;
        if (joy_right) s->angle += SHIP_TURN_SPEED;

        s->thrust_on = joy_up;
        if (joy_up) {
            /* FIX_MUL(cos, thrust) = (cos >> 8) * (thrust >> 8). port: the
               SDK's 8.8 table for cos >> 8 (rounded, the original floored) */
            s->dx += (int16_t)(fix_cos(s->angle) * THRUST_8);
            s->dy += (int16_t)(fix_sin(s->angle) * THRUST_8);
            {
                int16_t vx = (int16_t)(s->dx >> 8), vy = (int16_t)(s->dy >> 8);
                if (mul16(vx, vx) + mul16(vy, vy) > (int32_t)(SHIP_MAX_SPEED >> 8) * (SHIP_MAX_SPEED >> 8)) {
                    s->dx = slow15(s->dx);
                    s->dy = slow15(s->dy);
                }
            }
        }

        /* dx = (dx * 253) / 256, dy too (drag); x += dx, y += dy, wrapped
           (if x < 0 x += W; if x >= W x -= W); px, py = the whole parts */
        ship_move(s);

        if (s->invuln_timer > 0) s->invuln_timer--;

        if (joy_fire && (gs->frame & 7) == 0) {     /* one shot every 8 frames */
            fire_bullet(gs);
            sfx_shoot();
        }
    }

}

void game_update(GameState *gs, uint8_t joy_left, uint8_t joy_right,
                 uint8_t joy_up, uint8_t joy_fire)
{
    uint8_t i, j;
    Ship *s = &gs->ship;
    (void)j;

    gs->frame++;

    if (gs->state == STATE_GAMEOVER) {
        gs->state_timer--;
        /* port: the original started a new game here; this one goes on to
           the high-score table and the title (main.c) */
        if (gs->state_timer <= 0) gs->state = STATE_TITLE;
        goto update_particles;
    }

    if (gs->state == STATE_DEAD) {
        gs->state_timer--;
        if (gs->state_timer <= 0) {
            ship_reset(s);                  /* respawn in the middle */
            gs->state = STATE_PLAYING;
        }
        goto update_rocks_and_particles;
    }

    ship_controls(gs, joy_left, joy_right, joy_up, joy_fire);

    /* -- bullets -- */
    mw_ptr = gs->bullets;
    mw_n = MAX_BULLETS;
    mw_stride = sizeof(Bullet);
    mw_core();
    {
        Bullet *b = gs->bullets;
        for (i = 0; i < MAX_BULLETS; i++, b++)
            if (b->active && --b->life <= 0) b->active = 0;
    }

    /* -- bullet vs rock -- (port: circle tests in whole pixels, the
       difference of the whole pixels instead of the whole part of the
       difference: at most 1 px off) */
    if (!hits_valid) build_rock_hits(gs);
    {
        Bullet *b = gs->bullets;
        for (i = 0; i < MAX_BULLETS; i++, b++) {
            Rock *r;
            if (!b->active) continue;
            j = hit_first(b->px, b->py, 2, rock_hits, n_rock_hits);
            if (j == 0xFF) continue;
            r = &gs->rocks[rock_hits[j].id];
            b->active = 0;
            switch (r->size) {
            case ROCK_LARGE:  gs->score += SCORE_LARGE; break;
            case ROCK_MEDIUM: gs->score += SCORE_MEDIUM; break;
            default:          gs->score += SCORE_SMALL; break;
            }
            game_spawn_particles(gs, r->x, r->y,
                r->size == ROCK_LARGE ? 12 : (r->size == ROCK_MEDIUM ? 8 : 5));
            split_rock(gs, r);
            r->active = 0;
            gs->rock_count--;
            if (r->size == ROCK_LARGE) sfx_explode_large();
            else sfx_explode_small();
            build_rock_hits(gs);
        }
    }

    /* -- ship vs rock -- */
    if (s->alive && s->invuln_timer == 0 &&
        hit_first(s->px, s->py, SHIP_RADIUS, rock_hits, n_rock_hits) != 0xFF) {
        s->alive = 0;
        game_spawn_particles(gs, (pos7)(s->x >> 9), (pos7)(s->y >> 9), 20);
        sfx_die();
        gs->lives--;
        if (gs->lives <= 0) {
            gs->state = STATE_GAMEOVER;
            gs->state_timer = 150;
        } else {
            gs->state = STATE_DEAD;
            gs->state_timer = 100;
        }
    }

update_rocks_and_particles:
    /* the rocks' circles for the next frame's collision tests are collected
       as they move (bh_n counts them) */
    mw_ptr = gs->rocks;
    mw_n = MAX_ROCKS;
    mw_stride = sizeof(Rock);
    mw_hits = rock_hits;
    bh_n = 0;
    mw_core();
    mw_hits = 0;
    n_rock_hits = bh_n;
    hits_valid = 1;

    if (gs->rock_count <= 0 && gs->state == STATE_PLAYING) {
        gs->level++;
        game_spawn_rocks(gs, TUNE_START_ROCKS + (gs->level - 1) * TUNE_ROCKS_PER_LEVEL);
    }

update_particles:
    /* port: a particle that leaves the world is gone (the original kept
       moving it off screen, slowing down, without drawing it), and
       v - (v >> 4) for (v * 240) / 256 */
    if (particles_live) {
        pm_ptr = gs->particles;
        pm_n = MAX_PARTICLES;
        pm_core();
        particles_live = pm_live;
    }
}
