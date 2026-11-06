/*
 * zxtext.h - draw text straight into screen memory with the ROM font.
 *
 * Works without a terminal driver (STARTUP=31), on 48K and Next (the 48K
 * BASIC ROM, which holds the font at 0x3D00, is paged in for .tap programs
 * loaded from 48K BASIC and for .nex files). Not with the 128K editor ROM.
 */
#ifndef ZXTEXT_H
#define ZXTEXT_H

/* Print s at character cell (col 0-31, row 0-23); clips at the right edge. */
extern void zx_print_at(unsigned char col, unsigned char row, const char *s);

/* Set the attribute of `len` cells from (col,row) (e.g. PAPER_RED|INK_WHITE). */
extern void zx_attr_at(unsigned char col, unsigned char row, unsigned char len, unsigned char attr);

#endif
