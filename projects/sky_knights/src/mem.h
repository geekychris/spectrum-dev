/*
 * Sky Knights memory map. The code runs from 32768 up (0x4000-0x7FFF is
 * contended: the ULA slows the CPU there while it draws the screen, and the
 * game's C ran ~40% slower from there). Tables read less often sit in
 * 0x6000-0x7FFF instead (the sprite images, read by every blit, do not):
 */
#ifndef MEM_H
#define MEM_H
#include <stdint.h>

#define MEM_PRE_EGG    0x6000     /* the pod pre-shifted: 96 */
#define MEM_SX_TAB     0x6080     /* world x -> screen x: int16 x 341 */
#define MEM_SY_TAB     0x6340     /* world y -> screen y: 257 */
#define MEM_CELLS_BG   0x6500     /* the playfield's attributes: 768 (cells.asm) */
#define MEM_RING       0x6800     /* input of the last 64 frames: 3 x 64 */
#define MEM_END        0x68C0
#define MEM_TEST_TAPE  0x6900     /* test.py: ticks of input recorded / played back */
#define MEM_TEST_TAPE_LEN 2304
                                  /* (test.py's C reference build starts at 0x7200) */

#endif
