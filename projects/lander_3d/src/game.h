/* game.c: Lander 3D's game shell (the original's lander.h and game.c) */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include <arch/zxn.h>
#include "zxgame.h"
#include "ld.h"
#include "view.h"

enum { LS_TITLE, LS_PLAY, LS_RESULT, LS_OVER, LS_ENTRY };   /* port: LS_ENTRY, the initials */
#define MODE_TOUR W_COUNT               /* the menu's worlds, then the tour */

typedef struct {
    uint8_t state;
    int16_t state_timer;
    uint8_t menu;                       /* 0..2 a world, 3 the tour */
    int8_t ships;
    int16_t level;
    int32_t score, last_score;
    uint8_t paused, help, demo;         /* demo: the autopilot is flying */
    fix steady;                         /* the steady throttle (Q/E) */
    char msg[24], msg2[40];
    uint16_t frame;
    uint8_t title_page;                 /* port: the title and the high scores in turn */
    uint8_t topped;                     /* the tank was topped up (the probe) */
} Lander;


extern Lander lander;
extern Level level;
extern uint8_t hud_dirty;
extern hs_editor ed;
extern int8_t ed_place;
void game_init(void);
void game_frame(uint8_t frames);         /* one pass of the main loop, frames since the last */
void game_sample(void);                  /* from the frame interrupt (frame_hook) */

/* memory (main.c): bank 20 (the rules) with page 46 (the autopilot), bank
   22 (the view) with page 47 (its drawing), or bank 24 (the text) at
   0x0000 / 0x6000; the rest from 0x8000 */
#define LOGIC() do { ZXN_WRITE_MMU0(40); ZXN_WRITE_MMU1(41); ZXN_WRITE_MMU3(46); } while (0)
#define VIEW()  do { ZXN_WRITE_MMU0(44); ZXN_WRITE_MMU1(45); ZXN_WRITE_MMU3(47); } while (0)
#define HUD()   do { ZXN_WRITE_MMU0(48); ZXN_WRITE_MMU1(49); } while (0)
#endif
