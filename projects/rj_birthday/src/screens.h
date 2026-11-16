/* screens.c: the screens other than play, and the play screen's text */
#ifndef SCREENS_H
#define SCREENS_H
#include "game.h"
void hud_reset(void);
void hud_draw(const GameState *gs);
void world_text(const GameState *gs);
void screen_enter(const GameState *gs);   /* a new state: draw its screen */
void screen_frame(const GameState *gs);   /* each frame: what changes on it */
#endif
