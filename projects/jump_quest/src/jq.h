/*
 * Jump Quest - the original's game logic (amiga_mcp/examples/jump_quest:
 * game.h, player.c, enemy.c, items.c, level.c and main.c's state machine),
 * without the drawing, at a fixed 50 Hz. Builds for the Spectrum and for
 * the host (tools/solve.c plays it there).
 *
 * The front end (main.c) calls jq_new_game() after the title, then
 * jq_tick(input) once a tick, and draws from `game` and the events.
 */
#ifndef JQ_H
#define JQ_H

#include <stdint.h>

/* Screen. port: the Spectrum shows 16 of the original's 20 tiles across */
#define SCREEN_W        256
#define TILE_SIZE       16
#define TILES_Y         14

/* Physics (fixed-point 10.6) */
#define FP_SHIFT        6
#define FP(x)           ((x) << FP_SHIFT)
#define FP_INT(x)       ((x) >> FP_SHIFT)
#define GRAVITY         6
/* port: the original's -78 jumps 14 pixels, but its levels need 32 (the
   pipes in level 1) to 48 (level 3's climbs): no one could finish level 1.
   tools/solve.c clears level 3 from -172 on; -180 jumps 56 pixels;
   README.md, "What changed". */
#ifndef JUMP_FORCE
#define JUMP_FORCE      (-180)
#endif
#define MOVE_SPEED      FP(2)
#define MAX_FALL        FP(6)

/* Player dimensions */
#define PLAYER_W        14
#define PLAYER_H        28
#define PLAYER_H_SHORT  24  /* Dale */

/* Max entities */
#define MAX_ENEMIES     16
#define MAX_ITEMS       24
#define MAX_PARTICLES   16

/* Tile types */
#define TILE_EMPTY      0
#define TILE_GROUND     1
#define TILE_GRASS      2
#define TILE_BRICK      3
#define TILE_QBLOCK     4
#define TILE_STONE      5
#define TILE_PIPE_TL    6
#define TILE_PIPE_TR    7
#define TILE_PIPE_BL    8
#define TILE_PIPE_BR    9
#define TILE_PLATFORM   10
#define TILE_CLOUD_L    11
#define TILE_CLOUD_R    12
#define TILE_QBLOCK_HIT 13
#define TILE_FLAG       14
#define TILE_BUSH       15
#define NUM_TILES       16

/* Entity types */
#define ENT_NONE        0
#define ENT_PLAYER_START 1
#define ENT_BEETLE      2
#define ENT_FLY         3
#define ENT_SPIDER      4
#define ENT_ANT         5
#define ENT_BOARD       10
#define ENT_DISKETTE    11
#define ENT_BALL        12
#define ENT_PILLOW_C    13
#define ENT_PLANT       14
#define ENT_BRIEFCASE   20
#define ENT_HEART       21
#define ENT_PILLOW_P    22
#define ENT_BEER        23
#define ENT_WINE        24
#define ENT_LEVEL_END   30

/* Game states */
#define STATE_TITLE     0
#define STATE_PLAYING   1
#define STATE_DYING     2
#define STATE_LEVELWIN  3
#define STATE_GAMEOVER  4

#define NUM_LEVELS      3

/* Character select */
#define CHAR_RJ         0
#define CHAR_DALE       1

/* Input bits */
#define INP_LEFT        0x01
#define INP_RIGHT       0x02
#define INP_UP          0x04
#define INP_DOWN        0x08
#define INP_JUMP        0x10
#define INP_START       0x20

/* Direction */
#define DIR_RIGHT       1
#define DIR_LEFT        (-1)

/* the original's sound calls, as events for the front end (game.events) */
#define EV_JUMP         0x0001
#define EV_STOMP        0x0002
#define EV_COLLECT      0x0004
#define EV_POWERUP      0x0008
#define EV_HURT         0x0010
#define EV_DIE          0x0020
#define EV_LEVELWIN     0x0040
#define EV_LEVEL        0x0080      /* a level (re)started: draw it all */
#define EV_GAMEOVER     0x0100
#define EV_TILE         0x0200      /* level_set_tile: game.tile_tx/ty */
#define EV_PARTICLES    0x0400

typedef struct {
    uint8_t type;
    uint8_t tx, ty;    /* tile coordinates */
} EntitySpawn;

typedef struct {
    int8_t  type;
    int16_t x, y;      /* pixel position (world coords) */
    int16_t vx, vy;    /* velocity (fixed-point) */
    int8_t  dir;
    int8_t  alive;
    int8_t  frame;
    int8_t  timer;
} Enemy;

typedef struct {
    int8_t  type;
    int16_t x, y;      /* pixel position */
    int8_t  active;
    uint8_t frame;
    int8_t  collected;
    int8_t  float_off; /* bobbing offset */
} Item;

typedef struct {
    int16_t x, y;
    int16_t vx, vy;
    int8_t  life;
    int8_t  color;
} Particle;

typedef struct {
    uint8_t width;     /* in tiles */
    const uint8_t *tiles;    /* packed (zxrle) */
    const EntitySpawn *entities;
    const char *name;
} LevelDef;

typedef struct {
    int16_t x, y;       /* pixel position (world coords) */
    int16_t vx, vy;     /* velocity (fixed-point) */
    int8_t  dir;
    int8_t  on_ground;
    int8_t  character;  /* CHAR_RJ or CHAR_DALE */
    int8_t  health;
    int8_t  max_health;
    int8_t  lives;
    int32_t score;
    int8_t  state;      /* 0=idle, 1=walk, 2=jump, 3=fall */
    int8_t  frame;
    int8_t  frame_timer;
    uint8_t invuln;
    uint8_t speed_boost;
    int8_t  walk_frame;
} Player;

typedef struct {
    int8_t  state;
    int8_t  current_level;
    int8_t  num_players;   /* 1 or 2 */
    int8_t  active_player;
    int16_t cam_x;         /* camera x in world pixels */
    int16_t transition;    /* state transition timer */
    Player player[2];
    Enemy enemies[MAX_ENEMIES];
    Item  items[MAX_ITEMS];
    Particle particles[MAX_PARTICLES];
    /* port: for the front end and the bots */
    uint16_t events;       /* EV_* of the last jq_tick */
    uint8_t tile_tx, tile_ty;
    uint16_t level_ticks;  /* ticks since the level (re)started */
    uint8_t tiles[150 * TILES_Y];   /* the current level's tiles (changed by ? blocks) */
} Game;

extern Game game;
extern const LevelDef levels[NUM_LEVELS];

/* level.c's functions (jq.c) */
uint8_t level_get_tile(int16_t tx, int16_t ty);
uint8_t level_is_solid(int16_t tx, int16_t ty);
int16_t level_width_pixels(void);
const LevelDef *level_current(void);

void jq_new_game(uint8_t character, uint8_t num_players);
void jq_tick(uint8_t input);
/* re-point the level's row table at game.tiles (after copying a Game) */
void jq_restore(void);

#endif
