/*
 * prof.h - where a frame's time goes, for the speed work. Off unless
 * LD_PROF is defined (`bin/zx-make -C projects/lander_3d clean`, then
 * `bin/zx-make -C projects/lander_3d PROF=1`; tools/frame_prof.py reads
 * the log). PROF(id) ends the section running and starts section id; the
 * time is the raster line (NextReg 0x1E/0x1F) and the frames counted by
 * the interrupt, 312 lines a frame (~1,800 T-states each at 28 MHz).
 * game.c logs the sums, in lines, with the perf line.
 */
#ifndef PROF_H
#define PROF_H
#include <stdint.h>

enum { PR_OTHER, PR_RULES, PR_HUDV, PR_CAM, PR_GSET, PR_PROJ, PR_OBJS, PR_SKY, PR_GROUND, PR_POLYS, PR_TEXT, PR_CRAFT, PR_AP, PR_N };
#ifdef LD_PROF
extern uint16_t prof_sum[PR_N];
void prof_mark(uint8_t id) __z88dk_fastcall;
#define PROF(id) prof_mark(id)
#else
#define PROF(id) ((void)0)
#endif
#endif
