/*
 * SKY KNIGHTS - game definitions (port of amiga_mcp/examples/sky_knights/game.h).
 *
 * The rules run in the original's 320 x 256 world; only the drawing maps it
 * onto the Spectrum's 256 x 192 (main.c). Changes are marked "port:".
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include "zxgame.h"     /* first: its 8.8 FIX/FIX_INT give way to the game's below */

typedef int16_t WORD;
typedef uint16_t UWORD;
typedef int32_t LONG;

/* Screen dimensions (the original's world) */
#define SCREEN_W    320
#define SCREEN_H    256

/* port: positions and speeds are 16-bit with 6 fraction bits (1/64 pixel)
   instead of 16.16 in 32 bits: 32-bit arithmetic is slow on the Z80. Every
   constant below is exact at 1/64 except gravity (0.3 = 19.2/64), which
   game.c adds as 19 or 20 so that it averages 19.2. */
typedef int16_t Fixed;
#undef FIX
#undef FIX_INT
#undef FIX_FRAC
#define FIX_SHIFT    6
#define FIX(x)       ((Fixed)((x) * 64))
#define FIX_INT(x)   ((WORD)((x) >> FIX_SHIFT))

#define GRAVITY_LO      19      /* 0.3 = 19.2/64: 19 four frames in five, 20 the fifth */
#define FLAP_IMPULSE   (-192)   /* -3.0 */
#define MAX_FALL_VEL    256     /* 4.0 */
#define MAX_RISE_VEL   (-320)   /* -5.0 */
#define MOVE_SPEED      128     /* 2.0 */
#define FRICTION        8       /* 8192/65536 = 0.125 */
#define ENEMY_SLOW      64      /* 1.0 */
#define ENEMY_MED       96      /* 1.5 */
#define ENEMY_FAST      128     /* 2.0 */

/* Platform definitions */
#define MAX_PLATFORMS   8
#define GROUND_Y        236     /* ground level */
#define PLAT_THICK      8       /* platform thickness */

typedef struct {
    WORD x, y, w;   /* position and width in pixels */
} Platform;

/* Knight dimensions (player and enemy, same size) */
#define PLAYER_W    20
#define PLAYER_H    20
#define STRIKE_H    4   /* height of strike hitbox from top */

/* Enemy types */
#define ETYPE_SWOOPER    0
#define ETYPE_RAIDER     1
#define ETYPE_WRAITH     2

#define MAX_ENEMIES     8
#define MAX_EGGS        8
#define MAX_PLAYERS     2

/* Pod (dropped when an enemy is downed; cracks open into a new enemy) */
typedef struct {
    Fixed x, y;
    Fixed dy;
    WORD  active;
    WORD  timer;      /* crack countdown */
    WORD  on_ground;
    WORD  hatch_type; /* what enemy type it cracks into */
} Egg;

/* Enemy */
typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  type;       /* ETYPE_* */
    WORD  active;
    WORD  facing;     /* 0=right, 1=left */
    WORD  anim_frame;
    WORD  anim_timer;
    WORD  flap_timer; /* frames until next flap */
    WORD  on_ground;
} Enemy;

/* Player */
typedef struct {
    Fixed x, y;
    Fixed dx, dy;
    WORD  active;
    WORD  alive;
    WORD  lives;
    WORD  facing;     /* 0=right, 1=left */
    WORD  anim_frame;
    WORD  anim_timer;
    WORD  on_ground;
    WORD  respawn_timer;
    WORD  flapping;   /* visual: wings up */
} Player;

/* Game states */
#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_WAVE_INTRO 2
#define STATE_DYING      3
#define STATE_GAMEOVER   4

/* Sound event flags (set by game, cleared by main after triggering) */
#define EV_FLAP      1
#define EV_KILL      2
#define EV_EGG       4
#define EV_DIE       8
#define EV_WAVE      16

/* Input bits */
#define INP_LEFT     1
#define INP_RIGHT    2
#define INP_FLAP     4
#define INP_ESC      8
#define INP_START1   16   /* port: key 1, or FIRE on "1 PLAYER" */
#define INP_START2   32   /* port: key 2, or FIRE on "2 PLAYERS" */
#define INP_SELECT   64   /* port: toggles the title selection (was FLAP) */

/* Input state for both players */
typedef struct {
    UWORD p1;   /* port: keys (QAOP, SPACE, Sinclair 2); everything in a 1-player game */
    UWORD p2;   /* port: Kempston, Sinclair 1 */
    UWORD sys;
} InputState;

/* Game state */
typedef struct {
    Player    players[MAX_PLAYERS];
    Enemy     enemies[MAX_ENEMIES];
    Egg       eggs[MAX_EGGS];
    Platform  platforms[MAX_PLATFORMS];
    WORD      num_platforms;

    LONG      score[MAX_PLAYERS];
    WORD      wave;
    WORD      state;
    WORD      state_timer;
    WORD      frame;
    WORD      num_players;    /* 1 or 2 */
    WORD      enemies_alive;
    WORD      ev_flags;       /* sound event flags */

    WORD      title_selection; /* 0=1P, 1=2P */
} GameState;

/* Pod timing */
#define EGG_HATCH_TIME  300   /* frames (~5 sec at 50fps) */
#define EGG_WOBBLE_TIME 240   /* start wobbling at this point */

/* Scoring */
#define SCORE_SWOOPER   500
#define SCORE_RAIDER    750
#define SCORE_WRAITH    1000
#define SCORE_EGG       250
#define SCORE_WAVE      1000

/* Functions */
void game_init(GameState *gs);
void game_start(GameState *gs);
void game_update(GameState *gs, InputState *input);
void game_start_wave(GameState *gs);

#endif
