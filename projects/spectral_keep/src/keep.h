/*
 * Spectral Keep for the Spectrum Next: the game's rules, rooms and physics
 * (the original's keep.h, game.c, room.c, world.c), plus the Next front end.
 *
 * port: the original works in Q12 fixed point in 32-bit longs (4096 = one
 * floor tile). zsdcc's 32-bit arithmetic is library calls and the physics
 * runs every body against every other a few times a step, so here positions,
 * sizes and velocities are 16-bit Q11 (2048 = one tile; rooms are at most
 * 12 tiles across with their walls, 24576 < 32767). Directions stay Q12 unit
 * vectors (D12 = 4096); speeds are Q11 per step. Every constant is the
 * original's value in tiles converted (round(v * 2048)), with the original
 * Q12 value beside it. The same C builds on the host (tools/sim.c) and on
 * the Z80 with the same results.
 *
 * World units: one floor tile = 1.0 = 2048 (Q11). Velocities are Q11 per
 * logic step (50 steps/s), so the Unity per-second constants appear divided
 * by 50: walk 3.1 u/s = 127, jump 7.4 u/s = 303, gravity 22 u/s^2 = 18 per
 * step per step.
 */
#ifndef KEEP_H
#define KEEP_H

#include <stdint.h>

typedef int16_t fix;
#define FX        2048              /* port: Q11 (was 4096, Q12) */
#define FXS       11                /* shift for FX */
#define D12       4096              /* a unit direction (Q12) */
#define MAXH      6                 /* RoomData.MaxHeight */
#define MAXW      10                /* RoomData.MaxSize */

/* ---- level data (levels.c, generated from the original's levels.c) ---- */
enum { AT_GUARD, AT_HOUND, AT_GHOST, AT_BOUNCER, AT_SAGE };
typedef struct {
    int8_t type, x, y, z, alongz;
    const char *text;
} KActor;
typedef struct {
    const char *id, *name;
    int8_t gx, gy, w, d, ink;
    const char *seal;
    int8_t wall, floor;             /* wall: 0 brick 1 rock; floor: 0 slab 1 wood 2 cobble */
    const uint8_t *cells;           /* [y][z][x], z = 0 the front row; port: packed (zxrle.h) */
    const KActor *actors;
    uint8_t nactors;
} KRoom;
typedef struct {
    const char *name;
    uint8_t relics, start, sx, sy, sz;
    const KRoom *rooms;
    uint8_t nrooms;
    uint8_t first_gfx;              /* port: index of its first room in room_gfx[] */
    uint8_t nrelics;                /* port: relics in the keep (the original counts them) */
} KLevel;
extern const KLevel keep_levels[];
extern const uint8_t keep_nlevels;

/* port: the room being built's cells, unpacked into R.cells */
#define CELL(r, x, y, z) (R.cells[((y) * (r)->d + (z)) * (r)->w + (x)])
enum { SIDE_N, SIDE_E, SIDE_S, SIDE_W };
int8_t lv_room_at(const KLevel *l, int8_t gx, int8_t gy);       /* index or -1 */
int8_t lv_neighbour(const KLevel *l, uint8_t room, uint8_t side);
uint8_t lv_has_door(const KLevel *l, uint8_t room, uint8_t side);
void door_span(uint8_t len, uint8_t *lo, uint8_t *hi);

/* ---- physics (world.c, from Physics.cs) ---- */
typedef struct { fix x0, y0, z0, x1, y1, z1; } Box;
typedef struct Body {
    fix px, py, pz;                 /* centre of the bottom face */
    fix sx, sy, sz;                 /* size */
    fix vx, vy, vz;                 /* Q11 per step */
    uint8_t used, solid, dynamic, pushable, collides, player, npcbar, grounded;
    uint8_t harm;                   /* a monster: it and the player don't block each other */
    struct Body *ground;
    int8_t owner;                   /* actor index, -1 */
} Body;
#define MAXBODIES 48                /* port: 64 (the busiest room uses ~36) */
extern Body w_body[MAXBODIES];
extern uint8_t w_nbody;             /* port: bodies in use are below this */
extern Body w_gridbody;             /* "standing on a block" */
void  w_reset(uint8_t w, uint8_t d);
void  w_set_block(uint8_t x, uint8_t y, uint8_t z);
uint8_t w_block(int8_t x, int8_t y, int8_t z);
Body *w_add_static(fix x0, fix y0, fix z0, fix x1, fix y1, fix z1);
Body *w_add(fix px, fix py, fix pz, fix sx, fix sy, fix sz);
void  w_remove(Body *b);
void  body_box(const Body *b, Box *o);
uint8_t box_overlap(const Box *a, const Box *b, fix eps);
void  box_shrink80(Box *b);                                      /* port: only 80% was used */
void  box_grow(Box *b, fix g);
void  w_step(Body *b, uint8_t canpush, fix *moved);              /* gravity + move */
void  w_move(Body *b, fix dx, fix dy, fix dz, uint8_t canpush, fix *moved);
void  w_move_kinematic(Body *lift, fix dy);
uint8_t w_overlapping_solid(const Box *box, const Body *except);
fix   w_ground_below(fix x, fix y, fix z);                       /* top of what's under a point */

/* ---- rooms and actors (room.c, from RoomView.cs and Actors.cs) ---- */
enum { K_PLAYER, K_GUARD, K_HOUND, K_GHOST, K_BOUNCER, K_SAGE, K_CRATE };
typedef struct {
    uint8_t kind;
    Body *body;
    int16_t dx, dz;                 /* guard / bouncer heading, Q12 */
    uint8_t awake;
    int32_t bob, last_talk;
    const char *text;
    uint8_t face;                   /* 0..7: 0 = +z, 2 = +x, 4 = -z, 6 = -x */
    uint16_t phase;                 /* walk cycle, 256 = one stride pair */
    uint8_t roll;                   /* bouncer spin, 0..7 */
    int16_t invuln;                 /* player: ticks */
    int8_t coyote, jumpbuf;
    uint8_t was_grounded, visible;
    fix ground;                     /* port: top of what's below (shadows), room_grounds() */
} Actor;
typedef struct { Body *body; fix base; int8_t dir; int8_t wait; } Lift;
typedef struct { uint8_t kind, x, y, z; uint16_t cell; Box box; } Pickup;
typedef struct { Body *body; uint8_t x, y, z, rot; } Gate;
#define MAXACT   24
#define MAXLIFT  6
#define MAXPICK  12
#define MAXHAZ   48
#define MAXGATE  6
typedef struct {
    const KLevel *level;
    const KRoom *room;
    uint8_t ridx, host;             /* host = 0: title preview (no player, no pickups taken) */
    Actor act[MAXACT];
    uint8_t nact;
    int8_t player;                  /* index or -1 */
    Lift lift[MAXLIFT];
    uint8_t nlift;
    Pickup pk[MAXPICK];
    uint8_t npk;
    Box haz[MAXHAZ];
    uint8_t nhaz;
    Box throne;
    uint8_t has_throne, tx, ty, tz;
    Gate gate[MAXGATE];
    uint8_t ngate;
    int32_t clock;
    uint8_t exited;
    char cells[MAXH * MAXW * MAXW]; /* port: unpacked (the level data keeps them packed) */
} RoomState;
extern RoomState R;
void room_build(const KLevel *l, uint8_t ridx, uint8_t host);
void room_spawn_player(fix x, fix y, fix z);
void room_tick(int16_t mx, int16_t mz, uint8_t jump);   /* move: world direction, Q12, |m| <= 4096 */
void room_grounds(void);                                 /* port: each actor's ground, for the scene */
void room_entry_point(const KRoom *from, const KRoom *to, uint8_t side, fix *x, fix *y, fix *z);
int16_t isin256(uint8_t a);                              /* Q14 */

/* what a room tells the game (game.c) */
uint8_t host_taken(uint8_t ridx, uint16_t cell);
uint8_t host_gate_open(uint8_t ridx);
void host_pickup(uint8_t kind, uint8_t ridx, uint16_t cell);
uint8_t host_try_key(uint8_t ridx);
void host_death(const char *cause);
void host_exit(uint8_t side, fix x, fix y, fix z);
void host_throne(void);
void host_talk(const char *text);
void host_sound(uint8_t sfx);
void host_gate_opened(void);        /* port: the room's picture must lose its gates */

/* ---- the game (game.c, from Game.cs) ---- */
enum { GS_TITLE, GS_PLAYING, GS_DYING, GS_LEVELDONE, GS_GAMEOVER, GS_VICTORY };
/* the game's inks (the same numbers as arch/zx.h's INK_*, which it replaces) */
#undef INK_BLACK
#undef INK_BLUE
#undef INK_RED
#undef INK_MAGENTA
#undef INK_GREEN
#undef INK_CYAN
#undef INK_YELLOW
#undef INK_WHITE
#define INK_BLACK 0
#define INK_BLUE 1
#define INK_RED 2
#define INK_MAGENTA 3
#define INK_GREEN 4
#define INK_CYAN 5
#define INK_YELLOW 6
#define INK_WHITE 7
/* pad bits (the original's 3DO pad) */
#define B_UP    0x0001
#define B_DOWN  0x0002
#define B_LEFT  0x0004
#define B_RIGHT 0x0008
#define B_A     0x0010
#define B_B     0x0020
#define B_C     0x0040
#define B_P     0x0080
#define B_L     0x0200
#define B_R     0x0400
typedef struct {
    uint8_t state;
    int32_t timer, clock;
    uint8_t selected, level_index;
    const KLevel *level;
    int8_t lives, keys, relics;
    char message[96];
    uint8_t msg_ink;
    int32_t msg_until, last_nag;
    uint8_t visited[4];             /* port: 16 bytes; a keep has at most 10 rooms */
    uint8_t nvisited;
    uint8_t grid_controls, paused, music_on;
    fix entry_x, entry_y, entry_z;
    uint8_t flash_ink;
    int32_t flash_until, flash_len;
    uint8_t room_changed;           /* the scene must rebuild */
    uint8_t tour, tour_level;
    int8_t tour_room;               /* the title's room tour (L+R) */
    uint8_t msg_changed;            /* port: the HUD must redraw the message */
} Game;
extern Game G;
void game_init(void);
void game_step(uint16_t held, uint16_t pressed);
uint8_t game_message_visible(void);
void keep_log(const char *fmt, ...);

/* ---- sound (sound.c) ---- */
enum { SFX_JUMP, SFX_LAND, SFX_PICKUP, SFX_RELIC, SFX_KEY, SFX_GATE, SFX_DEATH, SFX_DOOR,
       SFX_TALK, SFX_BARK, SFX_LEVELDONE, SFX_START, SFX_BLIP, SFX_COUNT };
enum { T_TITLE, T_GATEHOUSE, T_CRYPT, T_TOWER, T_NONE };
void snd_play(uint8_t sfx);
void snd_music(uint8_t track);      /* T_NONE stops */
void snd_duck(int16_t ticks);
void snd_music_enable(uint8_t on);

/* ---- what tests read (probe.c): packed, the same bytes on the host and the Z80 ---- */
#ifndef __SDCC
#pragma pack(push, 1)
#endif
typedef struct {
    uint8_t state, level, room, lives, relics, keys, paused, nact, npk, player, ngate;
    fix px, py, pz;
    struct { uint8_t kind; fix x, y, z; } act[10];   /* every actor but the player (first 10) */
    struct { uint8_t kind, x, y, z; } pk[6];
    uint16_t tick;
} Probe;
#ifndef __SDCC
#pragma pack(pop)
#endif
extern Probe keep_probe;
void probe_update(void);

/* the iso projection, in pixels per unit: x -> (+14, -7), z -> (-14, -7), y -> (0, -17) */
#define ISO_X 14
#define ISO_Z 7
#define ISO_Y 17

#endif
