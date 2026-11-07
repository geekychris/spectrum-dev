// port: memory map (src/memmap.h). Code and levels fill ORG..0xF000, so the
// variables (BSS) go below the code, from 0x5D00 (the BASIC loader at 0x5CCB
// is done with once the program runs, as in rock_blaster).
#pragma output CRT_ORG_BSS = 0x5D00
