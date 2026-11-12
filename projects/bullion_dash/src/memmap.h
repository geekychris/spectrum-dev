/*
 * Bullion Dash: where things are in a 48K Spectrum's RAM.
 *
 *   0x5D00..        variables (BSS: the game state, the screen's cell lists,
 *                   the guards' distance field), over the BASIC loader
 *   ORG..0xF000     code and constant data (the levels: ~8 KB)
 *   0xF000-0xF100   the SDK's IM 2 vector table, its jump at 0xF1F1
 *   0xF101-0xF1F0   and 0xF200-0xFD58: tables made at start-up (below)
 *   0xFD58-0xFF58   the stack
 *
 * The parts above 0xF000 aren't in the .tap: they are filled at start-up.
 */
#ifndef MEMMAP_H
#define MEMMAP_H

#define HI_SCALE_X  0xF101      /* 136: the original's x / 2 -> room pixel column */
#define HI_PRE      0xF200      /* 13 pre-shifted sprite frames x 128 = 1664 */
#define HI_SCALE_Y  0xF880      /* 240: the original's y -> room pixel line */
#define HI_EDIT     0xF970      /* 224: the editor's level during a test play (nibbles) */
#define HI_KEPT     0xFA50      /* 224: the level the editor keeps (S / L, nibbles) */
#define HI_PATHQ    0xFC00      /* 256: gridbfs's queue (a 256-byte page) */
#define HI_TILEROW  0xFD00      /* 32: level.c's tile_row[16] (made at start-up) */
#define HI_GOLDHOME 0xFD20      /* 56: level.c's gold_home bits (made per level) */
#define HI_END      0xFD58

#endif
