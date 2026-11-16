/* Ace Pilot's view on the Next (render.c) */
#ifndef ACE_RENDER_H
#define ACE_RENDER_H
#include <stdint.h>
#include "game.h"
/* the view's box on the 256 x 192 Layer 2 back buffer (inclusive) */
extern void render_view(int16_t left, int16_t top, int16_t right, int16_t bottom);
extern void render_scene(const GameWorld *w, uint8_t player);
#endif
