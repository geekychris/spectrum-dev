/*
 * zxrle.h - a small run-length packer for data with long runs (level maps,
 * masks, mostly-empty tables), PackBits-style: packed on the host by
 * sdk/tools/zxrle.py, unpacked here.
 *
 *   rle_unpack(dst, src)   unpacks a whole stream; returns the bytes written
 *
 * Stream: a control byte c, then
 *   c < 0x80   c + 1 literal bytes (1..128)
 *   c > 0x80   one byte, repeated c - 0x7E times (3..129)
 *   c = 0x80   the end
 */
#ifndef ZXRLE_H
#define ZXRLE_H

#include <stdint.h>

extern uint16_t rle_unpack(uint8_t *dst, const uint8_t *src);

#endif
