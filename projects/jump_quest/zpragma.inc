// port: memory map. The code and tables fill ORG..0xF000, so the variables
// (BSS: the level, the view, the sprites' copies) go below the code, at
// 0x5D80, past the BASIC loader (0x5CCB).
#pragma output CRT_ORG_BSS = 0x5D80
