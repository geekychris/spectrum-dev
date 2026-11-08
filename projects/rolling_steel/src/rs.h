/*
 * Rolling Steel - the original's rs.h (3DO version of geekychris/rolling_steel,
 * by way of amiga_mcp/examples/rolling_steel), for the Spectrum Next.
 *
 * Fixed point throughout, as the original:
 *   positions, velocities  Q12 (4096 = one unit, or one unit per second)
 *   directions             Q12 at run time (the course files store Q14)
 *   angles                 65536 per turn
 * The physics steps at 50 Hz like Unity's FixedUpdate.
 *
 * port: zsdcc's long is 32 bits, as the original's; its int is 16 (every
 * int in the rules holds a small number). The physics is phys.asm, the same
 * rules in Z80N assembly, so its structures are laid out as rsmac.inc says
 * (checked in course_c.c). A course's triangles and collision grid live in
 * 8K pages (phys.asm maps them), not in C arrays.
 */
#ifndef RS_H
#define RS_H
#include <stdint.h>

typedef long w32;
typedef unsigned long uw32;
typedef w32 fix;
#define FX       4096L
#define FXF(v)   ((fix)((v) * 4096.0))
#define STEP_HZ  50
typedef struct { fix x, y, z; } V3;

typedef struct { fix cx, cy, cz, hx, hy, hz; w32 cs, sn; } OBB;          /* yaw-only box; cs/sn Q14 */
typedef struct { OBB box; w32 dir[3]; fix power; w32 kind; } Zone;     /* kind 0 fan, 1 boost; dir Q14 */
typedef struct { fix x, y, z, r, h; } Pillar;
typedef struct { fix x, y, z, len, speed, yaw; } Sweeper;                 /* speed deg/s, yaw deg (Q12) */
typedef struct { fix x, y, z, w, period, phase, lift; w32 cs, sn; } Crusher;
typedef struct { w32 kind; fix x, y, z, range, speed; w32 ax, az; } EnemySpec;   /* kind 0 chaser 1 blob */
typedef struct { fix x, y, z; w32 group; } CrumbleSpec;
typedef struct { fix x, y, z, safe, progress; } PathPt;                  /* progress Q16 */
#define MAXCRUMBLE 32
#define MAXPATH 200

/* port: what game.c reads of a course; the physics' share is in phy_* */
typedef struct {
    char name[33];
    w32 time_ms, gold_ms, silver_ms, bronze_ms, music;
    V3 spawn;
    V3 goal_world;
    fix kill_y;
    w32 npath;
    PathPt *path;
    uint8_t page;                  /* port: its first 8K page (course_c.c) */
} Course;
extern Course C;
int  course_load(int index);        /* 1..6; 0 on failure */
extern int course_count;

/* the physics' share of the course (phys.asm) */
extern OBB phy_goal, phy_acid[];
extern fix phy_kill_y;
extern uint8_t phy_nacid, phy_nzone, phy_npillar, phy_nsweep, phy_ncrush, phy_nenemy, phy_ncrumble;
extern Zone phy_zone[];
extern Pillar phy_pillar[];
extern Sweeper phy_sweep[];
extern Crusher phy_crush[];
extern EnemySpec phy_enemy[];
extern fix phy_gx0, phy_gz0;
extern uint16_t phy_gnx, phy_gnz;
extern uint8_t phy_gpage, phy_ipage, phy_tpage;
extern CrumbleSpec crumble_spec[MAXCRUMBLE];   /* course_c.c: for the view */

/* surface classes (collision) */
enum { S_DECK, S_ROUGH, S_ICE, S_RAIL, S_BOUNCY, S_COUNT };

/* ---- physics (phys.asm) ---- */
typedef struct {
    V3 p, v, w;                     /* position, velocity, angular velocity (rad/s, Q12) */
    fix r, inv_mass;                /* radius; 1/mass (Q12) */
    fix fric, bounce, gravity;      /* its own PhysX material and pull per step */
    fix rot_k;                      /* 1 / (0.4 r): spin from a friction impulse, Q12 */
    int grounded, frozen, live;
    V3 last_ground;
    int touching_enemy;             /* for collision-enter shoves */
    int touch_sweeper, touch_tile;
    const char *kill;               /* what got it this step, if anything */
    int goal;                       /* touched the finish pad */
} Ball;
typedef struct {
    int state;                      /* 0 solid, 1 shivering, 2 gone */
    long timer;                     /* steps */
    fix fall;                       /* how far it has dropped (visual) */
} Crumble;
typedef struct { Ball b; V3 home; int kind; fix range, speed; long ax, az; fix phase; } Enemy;
#define MAXENEMY 8
typedef struct {
    Crumble crumble[MAXCRUMBLE];
    Enemy enemy[MAXENEMY];
    int nenemy;
    long sweep_angle[8];            /* 65536 per turn, 8 fraction bits */
    fix crush_h[8];                 /* 0..1 lift of each crusher (Q12) */
    fix crush_clock[8];
    int crush_slammed[8];
    long clock;
} World;
extern World Wd;
void phys_reset(void);              /* a course was loaded */
void phys_reset_enemies(void);
void phys_world_step(void);         /* obstacles, enemies */
void phys_marble_step(Ball *b, fix wx, fix wz);
int  phys_ground_below(const V3 *p, fix *y);   /* port: p by pointer */
extern unsigned char phys_dt2;       /* port: play mode: one physics step is 2/50 s (phys.asm) */
extern int phys_sound;              /* SFX_* the physics wants played, or -1 */
extern Ball *phys_marbles[2];
extern int phys_nmarbles;
long isin(long a);                  /* Q14, a: 65536 per turn */
long icos(long a);
unsigned long isqrt(unsigned long v);
fix  vlenp(const V3 *a);            /* port: vlen(V3) by pointer */
extern const char str_crushed[], str_eaten[], str_dissolved[];

/* ---- camera (game.c's; the view only follows it: see view.c) ---- */
typedef struct {
    V3 focus;                       /* what it looks at */
    long yaw, pitch;                /* 65536 per turn */
    fix size;                       /* half height of the view, units */
} Cam;
enum { SP_MARBLE, SP_MARBLE2, SP_STEEL, SP_BLOB, SP_GHOST, SP_CRYSTAL, SP_CANDY, SP_GLOW, SP_SHADOW, SP_SPARK, SP_COUNT };

/* ---- sound (sound.c) ---- */
enum { SFX_START, SFX_GOAL, SFX_DEATH, SFX_CLACK, SFX_WARN, SFX_FALL, SFX_SIZZLE, SFX_CHOMP,
       SFX_SHATTER, SFX_THUD, SFX_COUNT };
void snd_play(int sfx);
void snd_music(int theme);          /* 0 title, 1..6 courses, -1 off */
void snd_roll(fix speed01);         /* rolling noise: 0 silent .. 4096 flat out */
#endif
