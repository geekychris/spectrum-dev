/*
 * wk.h - Warden Keep's rules (the original's game.h / game.cpp, in C):
 * one 8 x 8 room in isometric view, perimeter walls on the north and west
 * edges, barrels you push one tile, jewels you walk over; take all three
 * to win. Tile-locked steps, one per input edge (the auto-repeat is the
 * caller's, as in the original's main.cpp).
 *
 * port: a clock (WK_TIME ticks for the room; running out ends the game),
 * and a time bonus when the room is won, so the high-score table means
 * something. The original has neither.
 */
#ifndef WK_H
#define WK_H
#include <stdint.h>

#define WORLD_W 8
#define WORLD_H 8
enum { F_STONE, F_DOOR, F_HAZARD };
enum { O_NONE, O_WALL, O_BARREL, O_JEWEL };
enum { GM_TITLE, GM_PLAYING, GM_WIN, GM_OVER };     /* port: GM_OVER (the clock ran out) */

#define INPUT_LEFT  0x01
#define INPUT_RIGHT 0x02
#define INPUT_UP    0x04
#define INPUT_DOWN  0x08
#define INPUT_START 0x10

#define MAX_OBJECTS 64
#define WK_TIME     3000               /* port: 60 s at 50 ticks a second */
#define WK_BONUS    10                 /* port: points a second left */

typedef struct { uint8_t kind, wx, wy, anim; } Prop;
typedef struct { int8_t wx, wy; uint8_t facing, anim; } Player;    /* facing 0 N, 1 E, 2 S, 3 W */

/* events for the sound and the log */
enum { EV_STEP = 1, EV_PUSH = 2, EV_JEWEL = 4, EV_WIN = 8, EV_TIMEOUT = 16, EV_BUMP = 32 };

typedef struct {
    uint8_t mode;
    int16_t score;
    uint8_t jewels_left;
    int16_t state_timer;
    Player player;
    uint8_t floor[WORLD_H][WORLD_W];
    Prop objects[MAX_OBJECTS];
    uint8_t num_objects;
    uint16_t tick;
    int16_t time_left;                 /* port */
    int16_t bonus;                     /* port: the time bonus of a won room */
    uint8_t events;                    /* EV_* this tick */
    uint8_t moved;                     /* something to redraw */
} GameState;

void wk_init(GameState *g);
void wk_load_room(GameState *g);
void wk_tick(GameState *g, uint8_t input);
Prop *wk_object_at(GameState *g, int8_t wx, int8_t wy);
#endif
