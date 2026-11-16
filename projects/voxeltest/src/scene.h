/* voxeltest's landscape and views, shared by the Next (main.c) and the
   host reference (tools/host_ref.c) */
#ifndef SCENE_H
#define SCENE_H
#include <stdint.h>
#include "voxel.h"
extern int8_t heights[128 * 128];
extern uint8_t colours[32 * 32];
void scene_make(void);
void scene_view(uint8_t k);             /* camera k (0..2) and the lens */
#define SCENE_VIEWS 3
#endif
