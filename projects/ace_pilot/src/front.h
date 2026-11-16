/* Ace Pilot's front end on the Next (front.c): the tilemap HUD and text,
   the title and game over pictures on Layer 2, the sound effects. */
#ifndef ACE_FRONT_H
#define ACE_FRONT_H
#include <stdint.h>
#include "game.h"
extern void front_init(void);                       /* ROM at 0x0000 (its font) */
extern void front_title(uint8_t players);           /* both Layer 2 buffers + text */
extern void front_title_players(uint8_t players);
extern void front_play(void);                       /* clear the text for a game */
extern void front_hud(const GameWorld *w, uint8_t autopilot);  /* once a frame */
extern void front_overlay(const GameWorld *w);      /* on the back buffer, after the view */
extern void front_sound_init(void);                  /* after frame_init(): the sound runs in the interrupt */
extern volatile uint8_t music_want;                  /* 1: the Blue Danube plays */
#endif
