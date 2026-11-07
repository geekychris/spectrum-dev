; RAM in the banked parts of the memory map (see main.c): zsdcc can't put
; uninitialised arrays into a bank, so they are reserved here

SECTION BANK_17                 ; with the grid tables, while the logic runs
PUBLIC _pc_work
_pc_work: defs 3402             ; game.h's PcWork (the autopilot's arrays are the largest)

SECTION PAGE_38                 ; 0x4000 while drawing: the corners (rtab.h's Corner, xform.asm)
PUBLIC _cn_rec
_cn_rec: defs 488 * 16
