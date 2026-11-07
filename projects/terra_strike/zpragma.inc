// link addresses of the banked parts (Makefile, src/main.c): 8K page << 16 | address
// the simulation: world.c in bank 22 at 0x0000, world_setup.c in page 46 at 0x4000
#pragma output CRT_ORG_BANK_22 = 0x2C0000
#pragma output CRT_ORG_PAGE_46 = 0x2E4000
// the 3D view: render.c in bank 24 at 0x0000
#pragma output CRT_ORG_BANK_24 = 0x300000
// the HUD: hud.c in bank 26 at 0x0000
#pragma output CRT_ORG_BANK_26 = 0x340000
