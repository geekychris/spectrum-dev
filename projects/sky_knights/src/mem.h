/*
 * Sky Knights memory map. The code runs from 32768 up (0x4000-0x7FFF is
 * contended: the ULA slows the CPU there while it draws the screen, and the
 * game's C ran ~40% slower from there). The big tables sit in 0x6000-0x7FFF
 * instead, where only their reads pay for it:
 */
#ifndef MEM_H
#define MEM_H
#include <stdint.h>

#define MEM_PRE_RIDER  0x6000     /* 8 rider frames pre-shifted: 8 x 384 */
#define MEM_PRE_EGG    0x6C00     /* the pod pre-shifted: 96 */
#define MEM_SX_TAB     0x6C80     /* world x -> screen x: int16 x 341 */
#define MEM_SY_TAB     0x6F00     /* world y -> screen y: 257 */
#define MEM_CELLS_BG   0x7100     /* the playfield's attributes: 768 (cells.asm) */
#define MEM_END        0x7400

#endif
