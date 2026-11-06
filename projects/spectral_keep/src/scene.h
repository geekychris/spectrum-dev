/* The room on screen (scene.c). */
#ifndef SCENE_H
#define SCENE_H
#include <stdint.h>

void scene_room(void);                  /* a new room: palettes, picture, scenery list (slow: ~0.1 s) */
void scene_prepare(uint8_t show_player);/* work out the next frame's sprites and shadows */
void scene_commit(void);                /* show them (right after the frame interrupt) */
void scene_hide(void);                  /* no sprites, no shadows */
void scene_flash(uint8_t ink);          /* palettes tinted towards an ink (0 = normal) */
extern uint8_t scene_overflow;
#endif
