/* game.c: Drone Courier's shell (the original's game.c / courier.h) */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "dc.h"

enum { CS_TITLE, CS_PLAY, CS_OVER, CS_DONE };   /* port: CS_DONE, game over left (to the high scores) */

typedef struct {
    uint8_t state, start_day, autoplay, paused, help;
    uint8_t flash_timer, flash_pen, state_timer;
    char flash[32];
    uint16_t cam_yaw;                   /* the chase camera's heading, easing after the drone's */
} Courier;

/* for test.py (read by symbol, _dc_probe), written once a frame */
typedef struct {
    uint8_t state, day, jobs, jobs_done, fails, carrying, landed, autoplay;
    uint8_t help, paused, between, over, battery, goal;
    int32_t score, job_time, tick;
    int16_t x, y, z;
} GameProbe;

extern Courier co;
extern DCGame game;
extern GameProbe dc_probe;
void game_main(void);
/* game.c's tilemap layouts, shared with screens.c */
enum { SCR_NONE, SCR_TITLE, SCR_PLAY, SCR_HELP };
extern uint8_t screen;
extern char tmp[40];
void layout(uint8_t scr);
void hud_help(uint8_t page);            /* screens.c */
void initials(void);
#endif
