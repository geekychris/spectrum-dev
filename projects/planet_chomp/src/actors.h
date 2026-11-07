/* the sprites: the chomper, the spooks and the keys (the original's
   textures at the size their depth gives), the score popups, the radar's
   dots and the lives (actors.c, render.c's rd_sprites) */
#ifndef ACTORS_H
#define ACTORS_H
#include <stdint.h>

/* an actor: shown or not, its texture and size, its top left on the sprite plane */
typedef struct { int16_t x, y; uint8_t tex, size, show; } Act;
#define ACT_N 9                     /* 0 the chomper, 1..4 the spooks, 5..8 the keys */
extern Act act[ACT_N];
/* a popup / dot: position on the sprite plane and pattern (0: hidden) */
typedef struct { int16_t x, y; uint8_t pat; } Dot;
#define POP_N 8
#define DOT_N 10
extern Dot pop[POP_N], dot[DOT_N];
extern uint8_t lives_shown;
/* dot patterns (Dot.pat is this + 1): the spooks 0..3 (GLOOM .. MOSS), then */
#define DOT_FRIGHT 4
#define DOT_EYES 5
#define DOT_KEY 6
#define DOT_NEST 7
#define DOT_HERO 8

void act_init(void);                /* the fixed patterns, all hidden */
void act_commit(void);              /* right after the flip: patterns that changed, then attributes */
void rd_sprites(void);              /* render.c: after rd_frame, with its camera */
#endif
