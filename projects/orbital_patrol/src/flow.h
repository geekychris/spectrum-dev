/*
 * The original main loop's state machine (amiga_mcp/examples/orbital_patrol/
 * main.c), without the drawing: one call per 50 Hz frame. Shared by the
 * Next build and the host reference (tools/ref.c).
 */
#ifndef FLOW_H
#define FLOW_H
#include "game.h"

/* the title's start: a new game at wave 1 */
void flow_start(GameState *gs);
/* one frame of the states from STATE_LEVEL_START to STATE_GAMEOVER. When
   the game-over screen ends (its timer, or FIRE / ENTER) the state becomes
   STATE_HISCORE_ENTRY: the caller then shows the high scores (port: the SDK
   table) */
void flow_step(GameState *gs, WORD input);
#endif
