/* ball.c - the ball: its texture made from the puzzle, and drawn from a view */
#ifndef BALL_H
#define BALL_H
#include <stdint.h>
#include "bt_core.h"
#include "gfx_data.h"

#define BB_PEEK  1             /* every piece at home: the picture as it should be */
#define BB_MARKS 2             /* the chosen band (yellow rings) and half (cyan lines and ring) */

extern uint8_t ball_cols[BT_MAX_S + 1];   /* the texture's first column of each segment */
extern uint8_t ball_rows[BT_MAX_B + 1];   /* and row of each band */

/* the picture on the ball, cut for bands x segs: its palette, and which of its
   tiles look alike into p (bt_set_sig) */
void ball_picture(uint8_t pic, uint8_t bands, uint8_t segs, BtPuzzle *p);
/* only: just this band (a twist changed only it), 0xFF: all */
void ball_build(const BtPuzzle *p, uint8_t flags, uint8_t sel_band, uint8_t sel_half, uint8_t only);
uint8_t ball_half_u(uint8_t half);        /* the half's middle, in texels */
void ball_twist_begin(uint8_t band);
void ball_twist_shift(uint8_t band, uint8_t d);   /* the band's rows turned d texels east */
void ball_flip_begin(uint8_t half);       /* the flip's texture from the ball's */
/* draw into the back buffer, centred at x, y; off = the turn (texels west) */
void ball_draw(const View *v, uint8_t off, int16_t x, int16_t y);
void ball_draw_flip(uint8_t pitch, uint8_t half, uint8_t phi);   /* phi: 0..128 = 0..180 degrees */
#endif
