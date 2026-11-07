/* Orb Hunter's shell on the Next, shared by main.c, draw.c and sound.c */
#ifndef ORB_H
#define ORB_H
#include <stdint.h>
#include "game.h"

extern GameState gs;
extern uint8_t show_frame;                      /* counts drawn frames (flicker) */

/* draw.c */
void draw_init(void);
void draw_room_full(void);                      /* the room into Layer 2 (on entering it) */
void draw_room_update(void);                    /* changed tiles, lava, the save point */
extern uint8_t room_dirty;                      /* set when a tile changes (world.c) */
void draw_hud(void);
void draw_sprites(void);
void draw_hide_sprites(void);
void draw_title(void);                          /* the title picture, the table, the controls */
void draw_title_prompt(uint8_t on);
void draw_item_get(void);
void draw_gameover(uint8_t prompt);
void draw_name_entry(const char *name, uint8_t pos, int8_t rank);
void show_game(void);                           /* Layer 2: the game's screen */

/* sound.c */
void snd_init(void);
void snd_music(uint8_t on);
void snd_tick(void);
extern uint16_t snd_events;
extern uint8_t snd_sounding[3];

#endif
