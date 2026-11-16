/*
 * exec/types.h - the AmigaOS types the original's game.h, game.c and
 * levels.c use (they are compiled here unchanged). The sizes are the
 * Amiga's: WORD 16 bits, LONG 32, the same as zsdcc's int and long, so the
 * rules compute exactly what they compute on the Amiga.
 */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
typedef int8_t   BYTE;
typedef uint8_t  UBYTE;
typedef int16_t  WORD;
typedef uint16_t UWORD;
typedef int32_t  LONG;
typedef uint32_t ULONG;
#endif
