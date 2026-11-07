// port: memory map (src/memmap.h). Code and levels fill ORG..0xF000, so the
// variables (BSS) go below the code, at 0x5D80, past the BASIC loader.
#pragma output CRT_ORG_BSS = 0x5D80
