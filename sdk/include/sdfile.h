/*
 * sdfile.h - files on the Next's SD card through esxDOS / NextZXOS (rst 8),
 * for programs started from NextZXOS: a .nex run with .nexload, or code
 * called from BASIC with USR. The ROM must be paged in at 0x0000-0x3FFF
 * (don't call these with a bank mapped over it). Paths are esxDOS paths
 * ("/arcade/games.dat", or relative to the current directory). Under the
 * harness, start the emulator with an SD image (fatimg.py, ./zx arcade).
 *
 *   sd_load(path, dst, max)          up to max bytes -> dst; bytes read, or -1
 *   sd_save(path, src, len)          create / replace; bytes written, or -1
 *   sd_load_l2w(path, x, y, w, h)    a picture straight into Layer 2's
 *                                    320 x 256 mode: w columns of h bytes
 *                                    (column-major, as nextpic / arcade.py
 *                                    write them) at x, y; 0 if it can't be read
 */
#ifndef ZX_SDFILE_H
#define ZX_SDFILE_H
#include <stdint.h>

extern int16_t sd_load(const char *path, void *dst, uint16_t max);
extern int16_t sd_save(const char *path, const void *src, uint16_t len);
extern uint8_t sd_load_l2w(const char *path, uint16_t x, uint8_t y, uint16_t w, uint8_t h);

#endif
