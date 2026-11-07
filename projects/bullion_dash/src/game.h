/*
 * Bullion Dash - Game constants and shared types (the original's game.h).
 *
 * port: the logic keeps the original's coordinates (10 x 14 pixel tiles on a
 * 28 x 16 grid) so movement, timing and collisions are the original's; the
 * Spectrum draws each tile as one 8 x 8 character cell and scales positions
 * (render.c). Fields are 16-bit (zsdcc int), the score 32-bit. The tile map
 * is row-major (tiles[y][x]) - the SDK's tile-room renderer draws it as it
 * is - and changed through tile_set() so the renderer sees each change.
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/* Tile grid (logic units: the original's pixels) */
#define TILE_W     10
#define TILE_H     14
#define GRID_COLS  28
#define GRID_ROWS  16

/* Game states */
#define STATE_TITLE      0
#define STATE_PLAYING    1
#define STATE_DYING      2
#define STATE_LEVEL_DONE 3
#define STATE_GAMEOVER   4
#define STATE_EDITOR     5

/* Tile types */
#define TILE_EMPTY         0
#define TILE_BRICK         1
#define TILE_SOLID         2
#define TILE_LADDER        3
#define TILE_BAR           4
#define TILE_GOLD          5
#define TILE_HIDDEN_LADDER 6
#define TILE_PLAYER        7
#define TILE_ENEMY         8
#define TILE_TRAP          9
#define NUM_TILES          10

/* Limits */
#define MAX_ENEMIES  5
#define MAX_LEVELS   150
#define MAX_HOLES    32     /* port: dug bricks regrowing at once (see level.c) */

/* Timing */
#define BRICK_REGEN_FRAMES 150   /* 3 seconds at 50fps */
#define PLAYER_SPEED       2     /* pixels per frame */
#define ENEMY_SPEED        1

/* Directions */
#define DIR_NONE   0
#define DIR_LEFT   1
#define DIR_RIGHT  2
#define DIR_UP     3
#define DIR_DOWN   4

/* Player states */
#define PS_IDLE      0
#define PS_RUNNING   1
#define PS_CLIMBING  2
#define PS_HANGING   3
#define PS_FALLING   4
#define PS_DIGGING   5
#define PS_DEAD      6

/* Enemy states */
#define ES_RUNNING   0
#define ES_CLIMBING  1
#define ES_HANGING   2
#define ES_FALLING   3
#define ES_TRAPPED   4
#define ES_DEAD      5

#ifndef __SDCC
#pragma pack(push, 1)       /* host builds (tools/hostsim.c): the Spectrum's layout */
#endif

typedef struct {
    int16_t px, py;        /* pixel position */
    int8_t gx, gy;         /* grid position */
    uint8_t dir;           /* facing direction */
    uint8_t state;         /* PS_* */
    uint8_t anim;          /* animation frame counter */
    int8_t dig_timer;      /* frames remaining in dig action */
    uint8_t fall_count;    /* consecutive frames falling */
} Player;

typedef struct {
    int16_t px, py;        /* pixel position */
    int8_t gx, gy;         /* grid position */
    uint8_t dir;           /* facing direction */
    uint8_t state;         /* ES_* */
    uint8_t anim;          /* animation frame counter */
    int16_t trap_timer;    /* frames remaining in trap */
    uint8_t has_gold;      /* carrying stolen gold? */
    uint8_t active;        /* is this enemy slot in use? */
    uint8_t move_timer;    /* movement timing counter */
} Enemy;

/* port: the original's brick_timer[28][16] is a list of the dug bricks */
typedef struct {
    uint8_t x, y;
    uint8_t timer;         /* frames until the brick is back (0 = slot free) */
} Hole;

typedef struct {
    uint8_t state;         /* STATE_* */
    int16_t level_num;
    uint32_t score;
    int16_t lives;
    int16_t gold_total;
    int16_t gold_collected;
    uint16_t frame;
    Player player;
    Enemy enemies[MAX_ENEMIES];
    uint8_t num_enemies;
    uint8_t hidden_left;   /* port: hidden ladders not shown yet */
    uint8_t tiles[GRID_ROWS][GRID_COLS];   /* port: row-major */
    Hole holes[MAX_HOLES];
} GameState;

#ifndef __SDCC
#pragma pack(pop)
#endif

extern GameState gs;

/* bounds-checked tile lookup (out of bounds is solid) - the original's tile_at */
#define TILE(x, y) (gs.tiles[(y)][(x)])
extern uint8_t tile_at(int16_t x, int16_t y);
/* port: every tile change goes through here, so the screen redraws the cell */
extern void tile_set(int16_t x, int16_t y, uint8_t t);

/* port: sound effects (the original builds samples for these in sound.c but
   never plays them; the Spectrum plays them) */
#define SFX_DIG    1
#define SFX_GOLD   2
#define SFX_DEATH  3
#define SFX_LEVEL  4
#define SFX_TRAP   5
#define SFX_OVER   6
extern uint8_t sfx_pending;     /* the effect to play after the logic tick */

#endif /* GAME_H */
