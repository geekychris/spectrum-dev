/* the original's Amiga headers, for the host build of its sprites.c (gen_sprites.c) */
#ifndef STUB_EXEC_TYPES_H
#define STUB_EXEC_TYPES_H
#include <stdlib.h>
#define MEMF_CLEAR 0x10000
#define MEMF_CHIP 2
#define AllocMem(n, f) calloc(1, (n))
#define FreeMem(p, n) free(p)
#endif
