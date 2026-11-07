; RAM in the banked parts of the memory map (see main.c): zsdcc can't put
; uninitialised arrays into a bank, so they are reserved here

SECTION BANK_17                 ; with the grid tables, while the logic runs
PUBLIC _pc_work
_pc_work: defs 3402             ; game.h's PcWork (the autopilot's arrays are the largest)

SECTION PAGE_38                 ; 0x4000 while drawing: the corners (rtab.h's Corner, xform.asm)
PUBLIC _cn_rec
_cn_rec: defs 488 * 16

SECTION BANK_18                 ; after render.c's code: the draw list
PUBLIC _it_next, _it_a, _it_b, _it_ox, _it_oy, _it_type, _it_col, _rd_bucket
_rd_bucket: defs 256 * 2
_it_next: defs 360 * 2
_it_a:    defs 360 * 2
_it_b:    defs 360 * 2
_it_ox:   defs 360
_it_oy:   defs 360
_it_type: defs 360
_it_col:  defs 360
