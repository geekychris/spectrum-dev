/*
 * nextcopper.h - the Spectrum Next's copper: a co-processor that waits for a
 * raster position and writes Next registers, every frame, with no CPU time.
 * Good for per-line palette changes, gradients, split screens.
 *
 *   copper_begin()            start a new list (stops the copper)
 *   copper_wait(line, h)      wait until raster line `line` (0..311), at
 *                             horizontal position h (0..55, 8 pixels each)
 *   copper_move(reg, val)     write Next register reg (0..127)
 *   copper_end()              upload the list and run it from the top of
 *                             every frame (mode %11). Returns the number of
 *                             instructions (1024 at most)
 *
 * Line numbers are the ULA's: line 0 is the first pixel line of the 256 x 192
 * display, so the 320 x 256 Layer 2 row y is line y - 32, and its top 32
 * rows are lines 280..311 (50 Hz). The copper restarts at the vertical blank,
 * which is line 248 at 50 Hz (192 lines + 56 of bottom border): a list runs
 * 248..311 first (the top border), then 0..247, and its waits must come in
 * that order (copper_line_order() gives a line's place in it). A wait for a
 * line that has already gone by waits until the next frame's restart.
 *
 * The list is built in a RAM buffer (copper_buf, 2 bytes an instruction) and
 * sent with NextReg 0x60; copper_end() can be called again after editing.
 */
#ifndef NEXTCOPPER_H
#define NEXTCOPPER_H

#include <stdint.h>

#define COPPER_MAX 256          /* instructions buffered here (the copper holds 1024) */
#define L2Y_TO_LINE(y) ((uint16_t)((y) >= 32 ? (y) - 32 : (y) + 280))   /* 320x256 row -> line */
#define COPPER_START_LINE 248
/* a line's position in the copper's frame (0 = the restart at line 248) */
#define copper_line_order(l) ((uint16_t)((l) >= COPPER_START_LINE ? (l) - COPPER_START_LINE : (l) + 312 - COPPER_START_LINE))

extern uint8_t copper_buf[COPPER_MAX * 2];
extern uint16_t copper_n;

extern void copper_begin(void);
extern void copper_wait(uint16_t line, uint8_t h);
extern void copper_move(uint8_t reg, uint8_t val);
extern uint16_t copper_end(void);
extern void copper_stop(void);

#endif
