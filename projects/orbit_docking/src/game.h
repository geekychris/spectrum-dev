/* game.c: Orbit Docking's game shell (the original's game.c and dock.h) */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include <arch/zxn.h>
#include "od.h"

enum { DS_TITLE, DS_PLAY, DS_OVER, DS_ENTRY };

typedef struct {
    uint8_t state, start_level, autoplay, paused, help, title_page;
    int16_t state_timer;
    char flash[32];
    int16_t flash_timer;
    uint8_t flash_ink;
} Dock;

extern Dock dk;
extern ODGame game;
void game_init(void);
void game_frame(uint8_t frames);         /* one pass of the main loop, frames since the last */
void game_sample(void);                  /* from the frame interrupt (frame_hook) */

/* memory (main.c): bank 20 (the rules, od_core.c), bank 21 (the view,
   view.c) or bank 24 (the text, hud.c, screens.c) at 0x0000-0x3FFF; the
   rest from 0x8000 */
#define LOGIC() do { ZXN_WRITE_MMU0(40); ZXN_WRITE_MMU1(41); } while (0)
#define VIEW()  do { ZXN_WRITE_MMU0(42); ZXN_WRITE_MMU1(43); } while (0)
#define HUD()   do { ZXN_WRITE_MMU0(48); ZXN_WRITE_MMU1(49); } while (0)   /* bank 24: hud.c, screens.c */
#endif
