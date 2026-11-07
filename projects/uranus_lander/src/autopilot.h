/* Uranus Lander autopilot (autopilot.c): fills the input for one frame. */
#ifndef AUTOPILOT_H
#define AUTOPILOT_H
#include "game.h"
void autopilot_reset(void);
void autopilot_fly(const GameState *gs, InputState *in);
#endif
