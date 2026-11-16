/* front.h - Canyon Rail's game shell (front.c: the original's game.c) */
#ifndef FRONT_H
#define FRONT_H
#include <stdint.h>
#include "cr.h"

enum { RS_TITLE, RS_PLAY, RS_OVER, RS_ENTRY };

#define MAX_BURSTS 12
typedef struct { int8_t t; uint8_t size; int32_t x, y, z; } Burst;

/* resident (every bank sees it) */
typedef struct {
    uint8_t state, state_timer;
    uint8_t start_stage, autoplay, paused, help;
    char flash[32];
    uint8_t flash_timer, flash_pen;
    uint8_t hurt_timer, tracer_timer;
    int32_t best;
    uint16_t title_t;                   /* ticks on the title (its pages) */
    Burst burst[MAX_BURSTS];
} Rail;

extern Rail rl;
extern CRGame game;
extern unsigned play_in;                /* this tick's input to the rules */
extern uint8_t new_demo;                /* the title wants the next demo game (main.c, rules mapped) */
extern uint8_t start_stage_req;         /* start a game at this stage (main.c), 0: none */
extern uint8_t sfx_queue[4];

/* front.c (bank 20) */
void front_setup(void);                 /* the tilemap and its font, the palettes, the copper */
void front_init(void);
void front_input(uint8_t in, uint8_t edge, uint8_t keys);   /* keys: KEY_* latched */
void front_after_tick(void);            /* the events, the timers (after cr_tick) */
void front_draw(void);                  /* the tilemap: HUD, messages, screens */
void front_to_title(void);
void front_started(void);               /* main.c made the new game */
uint8_t front_scene(void);              /* draw the 3D view this frame? */

#define KEY_PAUSE 1
#define KEY_AUTO  2
#define KEY_HELP  4

enum { SFX_SHOT = 1, SFX_BOOM, SFX_HIT, SFX_BOLT, SFX_EFIRE, SFX_HURT, SFX_POD, SFX_BOSS, SFX_CLEAR, SFX_OVER,
       SFX_WIN, SFX_KEY };
void sfx(uint8_t id);                   /* main.c: queued for the interrupt */
#endif
