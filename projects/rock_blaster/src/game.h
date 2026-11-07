/*
 * Rock Blaster - game definitions (port of amiga_mcp/examples/rock_blaster/game.h).
 *
 * port: the world is 320 x 240 (the original's 320 x 256 cut to the
 * Spectrum's 4:3) and the screen shows it at 0.8 scale (256 x 192), so every
 * speed, radius and score is the original's. Positions are 16-bit (1/128 px)
 * except the ship's, which keeps the original's 16.16 so its thrust and drag
 * behave exactly the same; see game.c.
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#define WORLD_W 320
#define WORLD_H 240                         /* port: was 256 */

/* 16.16 for the ship (as the original) */
typedef int32_t Fixed;
#define FX16(x)      ((Fixed)(x) << 16)
#define FIX_PX(f)   ((int16_t)((f) >> 16))

/* port: everything else in 1/128 pixel, unsigned positions 0..WORLD*128 */
typedef uint16_t pos7;
typedef int16_t vel7;
#define P7(px)      ((uint16_t)(px) << 7)
#define V7(v)       ((vel7)((v) * 128))
#define W7          P7(WORLD_W)
#define H7          P7(WORLD_H)

/* Ship */
#define SHIP_MAX_SPEED  FX16(3)
#define SHIP_TURN_SPEED 3
#define SHIP_RADIUS     8
#define SHIP_INVULN_TIME 100

typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    uint8_t angle;          /* 0..255, 0 = right, 64 = down */
    uint8_t alive;
    int16_t invuln_timer;
    uint8_t thrust_on;
    int16_t px, py;         /* port: whole pixels, for collisions and drawing */
} Ship;

/* Bullet */
#define MAX_BULLETS     8
#define BULLET_LIFE     40

typedef struct {             /* the layout objs.asm expects (see there) */
    pos7 x, y;
    vel7 dx, dy;
    uint8_t active;
    int16_t px, py;
    int8_t life;
} Bullet;

/* Rock sizes */
#define ROCK_LARGE   0
#define ROCK_MEDIUM  1
#define ROCK_SMALL   2

#define MAX_ROCKS    24

typedef struct {
    pos7 x, y;
    vel7 dx, dy;
    uint8_t active;
    int16_t px, py;
    uint8_t size;
    uint8_t shape;          /* 0..3 */
} Rock;

/* Particle (explosions) */
#define MAX_PARTICLES 64

typedef struct {
    pos7 x, y;
    vel7 dx, dy;
    uint8_t active;
    uint8_t life;
} Particle;

/* Game state */
#define STATE_TITLE    0
#define STATE_PLAYING  1
#define STATE_DEAD     2
#define STATE_GAMEOVER 3

typedef struct {
    Ship       ship;
    Bullet     bullets[MAX_BULLETS];
    Rock       rocks[MAX_ROCKS];
    Particle   particles[MAX_PARTICLES];
    uint32_t   score;
    int16_t    lives;
    int16_t    level;
    uint8_t    state;
    int16_t    state_timer;
    int16_t    rock_count;
    uint16_t   frame;
} GameState;

/* Tunables (the original's defaults, *100) */
#define TUNE_ROCK_SPEED      80
#define TUNE_SHIP_THRUST     20
#define TUNE_BULLET_SPEED    400
#define TUNE_START_LIVES     3
#define TUNE_START_ROCKS     4
#define TUNE_ROCKS_PER_LEVEL 2

void game_init(GameState *gs);
void game_spawn_rocks(GameState *gs, int16_t count);
void game_update(GameState *gs, uint8_t joy_left, uint8_t joy_right,
                 uint8_t joy_up, uint8_t joy_fire);
void game_spawn_particles(GameState *gs, pos7 x, pos7 y, uint8_t count);

extern const uint8_t rock_radius_tab[3];
extern uint8_t particles_live;
#define rock_radius(size) (rock_radius_tab[size])

#define SCORE_LARGE  20
#define SCORE_MEDIUM 50
#define SCORE_SMALL  100

/* sound effect callbacks (main.c) */
void sfx_shoot(void);
void sfx_explode_large(void);
void sfx_explode_small(void);
void sfx_die(void);

#endif
