/*
 * input2.h - input for two players (or one player with a choice of
 * controls): each player reads only the sources given to them, so the two
 * don't steer each other, unlike input_read() in zxgame.h, which ORs every
 * key set and joystick together.
 *
 *   in2_init()            detects the Kempston interfaces (port 0x1F; port
 *                         0x37, the Next's second joystick in Kempston 2 mode)
 *   in2_scan()            reads the 8 keyboard half-rows and the joysticks
 *                         once (call once a frame, before in2_update)
 *   in2_bits(src)         the IN_* bits (zxgame.h) the sources in `src`
 *                         give from the last scan
 *   in2_update(&pad)      pad.held = in2_bits(pad.src); pad.pressed = the
 *                         bits that went down since the last update
 *
 * Sources (OR them together):
 *   IN2_QAOP      Q up, A down, O left, P right
 *   IN2_WASD      W up, S down, A left, D right
 *   IN2_IJKL      I up, K down, J left, L right
 *   IN2_CURSOR    5 left, 6 down, 7 up, 8 right, 0 fire (cursor joystick)
 *   IN2_SINCLAIR1 6 left, 7 right, 8 down, 9 up, 0 fire
 *   IN2_SINCLAIR2 1 left, 2 right, 3 down, 4 up, 5 fire
 *   IN2_KEMPSTON1 port 0x1F, IN2_KEMPSTON2 port 0x37 (if detected)
 *   IN2_SPACE     SPACE = fire, IN2_ENTER  ENTER = start
 * A key in two sets a player has (A in QAOP and WASD) means both; cursor
 * and Sinclair 1 share keys 6-8, so give them to different players only
 * if you accept the clash (or not at all).
 *
 * Presses are seen between updates: a tap shorter than the game's frame
 * can be lost (zxgame.h's input_latch covers one player's taps).
 * Works on the 48K/128K too (no Kempston 2 there).
 */
#ifndef INPUT2_H
#define INPUT2_H
#include <stdint.h>
#include "zxgame.h"

#define IN2_QAOP      0x0001
#define IN2_WASD      0x0002
#define IN2_IJKL      0x0004
#define IN2_CURSOR    0x0008
#define IN2_SINCLAIR1 0x0010
#define IN2_SINCLAIR2 0x0020
#define IN2_KEMPSTON1 0x0040
#define IN2_KEMPSTON2 0x0080
#define IN2_SPACE     0x0100
#define IN2_ENTER     0x0200

typedef struct { uint16_t src; uint8_t held, pressed; } in2_pad;

extern uint8_t in2_rows[8];          /* the last scan: half-rows 0xFEFE..0x7FFE, 0 bit = down */
extern uint8_t in2_kemp[2];          /* the last scan of the Kempstons (1 bit = pressed) */
extern uint8_t in2_has_kemp[2];      /* detected by in2_init */
extern void in2_init(void);
extern void in2_scan(void);
extern uint8_t in2_bits(uint16_t src);
extern void in2_update(in2_pad *p);
/* a key from the last scan: half-row 0..7 (0xFEFE = 0 .. 0x7FFE = 7), bit mask */
#define in2_key(row, mask) (!(in2_rows[row] & (mask)))
#endif
