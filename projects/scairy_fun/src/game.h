/* game.h - the game's resident side (main.c): memory map, palettes */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>

/* MMU slots 0-3 hold the simulation's banks or the renderer's */
void map_sim(void);
void map_render(void);
void set_palette(int route, int glare);

#endif
