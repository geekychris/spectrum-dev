/*
 * Emberkeep - game constants and state (the original's game.h).
 *
 * port: C instead of C++ (EmberGame's methods are game_* functions on the
 * global `gs`); fields are 16-bit (the original's LONGs hold pixels, small
 * counters and a score below 65536 a game), the score 32-bit for the high
 * scores; the frame counter `tick` is 16-bit. game.h packs the structs on
 * the host so a host build (tools/hostsim.c) shares the Spectrum's layout.
 */
#ifndef EMBERKEEP_GAME_H
#define EMBERKEEP_GAME_H

#include <stdint.h>

#define TILE_W          16
#define TILE_H          16
#define GRID_W          20
#define GRID_H          15
#define PLAY_W          (GRID_W * TILE_W)     /* 320 */
#define PLAY_H          (GRID_H * TILE_H)     /* 240 */
#define HUD_H           16
#define SCREEN_W        320
#define SCREEN_H        (PLAY_H + HUD_H)      /* 256 */

/* Tile kinds */
#define T_FLOOR    0    /* passable */
#define T_WALL     1    /* impassable */
#define T_RUNE     2    /* pickup: runes_remaining - 1; unlocks the portal at 0 */
#define T_PORTAL   3    /* exit, locked until runes_remaining == 0 */
#define T_OIL      4    /* pickup: refills the lantern */
#define T_HAZARD   5    /* deadly on touch */

/* Physics: top-down, no gravity, 4-directional walk */
#define WALK_SPEED          2        /* px per frame */
#define LIGHT_RADIUS_PX     40       /* half-side of the light square */
#define LANTERN_MAX         1000     /* fuel scale */
#define LANTERN_DRAIN_FRAMES 4       /* drain 1 unit per this many frames */
#define OIL_REFILL           400     /* per flask */

#define MAX_ENEMIES  6

#define GM_TITLE     0
#define GM_PLAYING   1
#define GM_WIN       2
#define GM_LOSE      3
#define GM_LEVELWIN  4

#define INPUT_LEFT   0x01
#define INPUT_RIGHT  0x02
#define INPUT_UP     0x04
#define INPUT_DOWN   0x08
#define INPUT_START  0x10     /* SPACE on title / end screens */

#define RUNE_SCORE       100
#define OIL_SCORE        25
#define LEVEL_SCORE      500

#ifndef __SDCC
#pragma pack(push, 1)
#endif

/* patrols horizontally or vertically; kills on contact, seen or not */
typedef struct {
    int16_t x, y;            /* pixel top-left */
    int16_t dx, dy;
    int16_t min_pos, max_pos;
    uint8_t axis;            /* 0 = horizontal, 1 = vertical */
    uint8_t colour;          /* palette hint: 4 cyan, 5 magenta, 6 yellow */
    uint8_t alive;
    uint8_t frame;
} Enemy;

typedef struct {
    int16_t x, y;
    uint8_t facing;          /* 0 up, 1 right, 2 down, 3 left */
    uint8_t frame;
} Player;

typedef struct {
    uint8_t mode;
    int16_t level_index;
    int16_t lives;
    int16_t runes_remaining;
    int16_t runes_total;
    uint32_t score;
    int16_t lantern;         /* 0..LANTERN_MAX */
    int16_t state_timer;
    Player player;
    uint8_t grid[GRID_H][GRID_W];
    Enemy enemies[MAX_ENEMIES];
    uint8_t num_enemies;
    int16_t spawn_x, spawn_y;
    uint16_t tick;
} GameState;

#ifndef __SDCC
#pragma pack(pop)
#endif

extern GameState gs;

void game_init(void);
void game_load_level(int16_t index);
void game_tick(uint8_t input);

/* port: what changed in the last tick, for the screen (the original
   redraws everything every frame): grid cells that changed, the level */
#define CHANGE_LEVEL  0x01   /* a new level (or a restart): redraw the room */
#define CHANGE_TILE   0x02   /* a pickup: changed_col/row is floor now */
#define CHANGE_DEATH  0x04   /* a life lost */
#define CHANGE_PICKUP_RUNE 0x08
#define CHANGE_PICKUP_OIL  0x10
#define CHANGE_EXIT   0x20   /* through the portal */
extern uint8_t game_changes;
extern uint8_t changed_col, changed_row;

#endif
