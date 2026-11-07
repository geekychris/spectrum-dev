// port: memory map. The game's code and tables (~31K) fill 0x7400..0xF000,
// so its variables (BSS, ~5.8K: the world, the attribute shadow map, the ship
// images) go below the code, at 0x5D80, past the BASIC loader (0x5CCB).
#pragma output CRT_ORG_BSS = 0x5D80
