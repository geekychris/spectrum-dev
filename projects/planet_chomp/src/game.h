/* Planet Chomp game logic (the original's game.h: GameDirector.cs, Ghost.cs,
 * Chomper.cs, Mover.cs): one call per 50 Hz step. */
#ifndef GAME_H
#define GAME_H
#include "pc.h"

#define TICKS(s) ((int32_t)((s) * 50))

enum { GS_TITLE, GS_READY, GS_PLAYING, GS_DYING, GS_CLEAR, GS_OVER };
enum { GM_NEST, GM_ACTIVE, GM_EATEN };
enum { GK_CHASER, GK_AMBUSHER, GK_FLANKER, GK_DRIFTER };

/* the pad bits game_step takes (the original's amiga3do.h) */
#define PAD_UP     0x0001
#define PAD_DOWN   0x0002
#define PAD_LEFT   0x0004
#define PAD_RIGHT  0x0008
#define PAD_A      0x0010
#define PAD_B      0x0020
#define PAD_C      0x0040
#define PAD_P      0x0080
#define PAD_L      0x0200
#define PAD_R      0x0400

typedef struct {
    int16_t from, to, prev;
    int8_t  exitk;
    uint8_t moving;
    int32_t t;                      /* Q16 along the edge */
    V3      heading;                /* Q14 */
    int16_t hd_from;                /* port: the edge heading was last set from */
    int8_t  hd_k;
} Mover;

typedef struct {
    Mover    mv;
    uint8_t  mode, fright, kind, slot;
    V3       scatter;
    int32_t  release_at;            /* life ticks */
    uint32_t rng;
} Ghost;

typedef struct { V3 world; int16_t pts; int16_t age; } Popup;

typedef struct {
    uint8_t  state, paused;
    int32_t  timer, life;           /* ticks in state, in this life */
    uint16_t clock;                 /* port: total ticks, 16 bits (only its low bits are used) */
    int32_t  score, hiscore;
    int8_t   lives;
    uint8_t  level, extra_given;
    int16_t  crumbs_left, crumbs_total;
    int32_t  power_left, power_total;
    uint8_t  scatter, mode_index, combo;
    int32_t  mode_timer;
    int16_t  player_speed, ghost_speed;   /* Q8 units per second */
    Mover    player;
    uint8_t  powered;
    Ghost    ghost[4];
    Popup    popup[8];
    Camera   cam;
    uint8_t  overview;
    uint16_t mouth;                 /* mouth phase, ticks of movement (port: 16 bits) */
    uint8_t  demo;                  /* attract mode: the autopilot plays */
    /* port: for the test and the HUD */
    uint8_t  ghosts_eaten, keys_taken;
} Game;

extern Game g;
extern uint8_t crumb[CELLS], key[CELLS];
extern int16_t nest_dist[CELLS];
extern const char *const ghost_name[4];

/* work arrays shared by the maze carving, the breadth-first searches and
   the autopilot (never in use at the same time) */
typedef union {
    struct { int16_t stack[CELLS], order[CELLS]; uint8_t seen[CELLS]; } carve;
    struct { int16_t dist[CELLS], parent[CELLS], q[CELLS]; uint8_t danger[CELLS]; } bot;
    int16_t bfs_q[CELLS];
} PcWork;
extern PcWork pc_work;

void game_init(void);
void game_step(uint16_t held, uint16_t pressed);
void mover_dir(V3 *r, const Mover *m);
int16_t mover_nearest(const Mover *m);
uint8_t ghost_dangerous(const Ghost *gh);
void ghost_world(V3 *r, const Ghost *gh);  /* sprite centre, world Q8 */
void player_world(V3 *r);

/* port: the autopilot's choice at cell c (the original's bot_choose), for
   the test's bot, which steers with the keys */
int8_t bot_exit(int16_t c);

/* the original's sfx.h (sound.c) */
enum { SFX_CRUMB_A, SFX_CRUMB_B, SFX_KEY, SFX_EAT, SFX_DEATH, SFX_START, SFX_CLEAR, SFX_EXTRA,
       SFX_HUM, SFX_FRIGHT, SFX_COUNT };
void sfx_play(uint8_t id);
void sfx_crumb(void);
void sfx_ambience(uint8_t which);      /* 0 silent, 1 hum, 2 bubbling (spooks scared) */
#endif
