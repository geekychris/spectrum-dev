/*
 * aystream.h - TurboSound (three AY chips on the Next) register-stream
 * player: music and effects made on the host as "these AY registers change
 * on this frame" (sdk/tools/aystream.py), played one frame at a time.
 *
 * Each chip plays one stream at a time, e.g. music on chips 0 and 1 and
 * effects on chip 2. Streams live in banked memory (8K pages) and may cross
 * page boundaries; while a stream byte is read the player maps its page into
 * MMU slot 2 (0x4000-0x5FFF) and puts the slot back after.
 *
 * Call as_tick() from the main loop, once per frame that passed (the count
 * frame_wait() returns), not from the interrupt: a Next register is written
 * as "select (port 0x243B), then value (0x253B)", and an interrupt between
 * the two that selects another register sends the program's value there.
 *
 *   as_init()                 silence all three chips
 *   as_play(chip, page, addr, loop)
 *                             start the stream at 8K page `page`, address
 *                             addr (0x4000 + offset in the page); loop = 1
 *                             to restart at its loop point when it ends
 *   as_stop(chip)             stop and silence a chip
 *   as_atten(chip, n)         lower the chip's tone volumes by n (0..15),
 *                             e.g. to duck music under an effect
 *   as_busy(chip)             1 while a stream plays
 *   as_tick()                 one frame of every playing stream (main loop)
 *
 * Stream format (aystream.py writes it): a 2-byte header, the loop point's
 * offset from the start (0xFFFF = none), then one record per frame:
 *   u8 mask0          bit r: register r (0..7) follows
 *   u8 mask1          bits 0..5: registers 8..13 follow; bit 6: a byte n
 *                     follows the values, then n more frames with no
 *                     changes; bit 7: end of stream (no values)
 *   values in register order
 */
#ifndef AYSTREAM_H
#define AYSTREAM_H

#include <stdint.h>

extern void as_init(void);
extern void as_play(uint8_t chip, uint8_t page, uint16_t addr, uint8_t loop);
extern void as_stop(uint8_t chip);
extern void as_atten(uint8_t chip, uint8_t n);
extern uint8_t as_busy(uint8_t chip);
extern void as_tick(void);

#endif
