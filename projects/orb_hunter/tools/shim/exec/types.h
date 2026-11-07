/* the AmigaOS types the original's game.h uses, for building its levels.c
   and game.c on the host (tools/gen_world.c, tests/host_diff.c) */
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
typedef int16_t WORD;
typedef uint16_t UWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef uint8_t UBYTE;
typedef int8_t BYTE;
#endif
