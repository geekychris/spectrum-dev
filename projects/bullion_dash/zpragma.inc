// port: memory map (src/memmap.h). Code and levels fill ORG..0xF000, so the
// variables (BSS) go below the code, from 0x5D00 (the BASIC loader at 0x5CCB
// is done with once the program runs, as in rock_blaster).
#pragma output CRT_ORG_BSS = 0x5D00
// no stdio heap: the game opens no FILE streams (startup 31, no terminal);
// its 256 bytes of BSS make room for the code (src/memmap.h)
#pragma output CLIB_STDIO_HEAP_SIZE = 0
