/* game.h - the game's resident side (main.c) and its front end (game.c) */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "sc.h"

/* main.c (resident) */
extern World world;
extern uint8_t render_skip;             /* profiling: bits skip parts of the frame (render.c) */
extern uint8_t sfx_queue;
extern uint8_t keys_latched;            /* V, H, S taps seen by the interrupt (bits 0-2) */               /* an effect for the interrupt to start (SFX_*) */
void map_sim(void);                     /* MMU slots 0-3: the simulation's banks */
void map_render(void);                  /*                the renderer's */
void map_front(void);                   /*                the front end's (game.c) */
void set_palette(int route, int glare); /* copper.c: page 42 at 0x6000, the renderer's bank mapped */


/* game.c (bank 20) */
enum { SFX_NONE, SFX_CHIME_ON, SFX_CHIME_OFF, SFX_BLIP, SFX_SAVE, SFX_SCRAPE, SFX_CONE,
       SFX_CRASH, SFX_SPLASH, SFX_ALERT, SFX_SIREN, SFX_KEY };
#define FR_PLAY   0x01                  /* run the simulation */
#define FR_3D     0x02                  /* draw the 3D view */
void ui_font(void);                     /* the tilemap's tiles */
void game_init(void);
uint8_t game_input(void);               /* keys -> the world's IN_* bits; FR_* for this frame */
extern unsigned game_in;                /* those bits */
extern uint8_t game_reset;              /* set: back to the title (world_init) */
void game_events(void);                 /* after the simulation steps: sounds, the log */
void game_draw(void);                   /* the tilemap: HUD, text, screens; the AI's view */

#endif
