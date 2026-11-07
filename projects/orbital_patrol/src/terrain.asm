; Orbital Patrol: the terrain columns entering the view (view.c's
; terrain_update loop, ~1,500 T-states a column in zsdcc's C, ~250 here).
; For screen columns tc_s0 .. tc_s1 - 1 of the viewport at world x tc_vx:
; world column wx = (tc_vx + s) mod 1920 is in Layer 2's physical column
; p = (tc_vx mod 320 + s) mod 320 (the playfield scrolls by tc_vx mod 320);
; if p doesn't already show wx's ridge, the old ridge (7 rows from col_h[p])
; is cleared to the transparent sky and the new one drawn from row
; terrain_h[wx] (none if tc_flat: the planet is gone).

SECTION code_user

PUBLIC _terrain_cols
PUBLIC _tc_vx, _tc_s0, _tc_s1, _tc_flat, _tc_terrain, _tc_drawn

DEFC COL_WX = 0x1900            ; view.c (page 46): int16[320]
DEFC COL_H  = 0x1B80            ; uint8[320]
DEFC L2_PAGE = 32               ; Layer 2 in banks 16-20: pages 32-41
DEFC SKY = 0xE3

_terrain_cols:
    push ix
    ; p0 = tc_vx mod 320
    ld hl,(_tc_vx)
    ld de,320
tc_mod:
    or a
    sbc hl,de
    jr nc,tc_mod
    add hl,de
    ld (tc_p),hl
    ld hl,(_tc_vx)
    ld de,(_tc_s0)
    add hl,de                   ; wx = vx + s0
    ld de,1920
    or a
    sbc hl,de
    jr nc,tc_wx0
    add hl,de
tc_wx0:
    ld (tc_wx),hl
    ld hl,(tc_p)
    ld de,(_tc_s0)
    add hl,de                   ; p = p0 + s0
    ld de,320
    or a
    sbc hl,de
    jr nc,tc_p0
    add hl,de
tc_p0:
    ld (tc_p),hl
    ld hl,(_tc_s1)
    ld de,(_tc_s0)
    or a
    sbc hl,de                   ; columns
    jp z,tc_done
    jp c,tc_done
    ld (tc_n),hl
tc_loop:
    ; h = tc_flat ? 0 : terrain_h[wx] (the low byte: 174..229)
    xor a
    ld c,a
    ld a,(_tc_flat)
    or a
    jr nz,tc_h
    ld hl,(tc_wx)
    add hl,hl
    ld de,(_tc_terrain)
    add hl,de
    ld c,(hl)
tc_h:                           ; C = h
    ; same as shown?  col_wx[p] == wx && col_h[p] == h
    ld hl,(tc_p)
    ld de,COL_H
    add hl,de
    ld a,(hl)
    ld b,a                      ; B = the old h
    cp c
    jr nz,tc_draw
    ld hl,(tc_p)
    add hl,hl
    ld de,COL_WX
    add hl,de
    ld a,(tc_wx)
    cp (hl)
    jr nz,tc_draw
    inc hl
    ld a,(tc_wx + 1)
    cp (hl)
    jr z,tc_next
tc_draw:
    ; map the column's page into slot 1: Layer 2 page 32 + p / 32
    ld hl,(tc_p)
    ld a,l
    srl h                       ; p / 32 (p < 320: H is 0 or 1)
    rra
    srl a
    srl a
    srl a
    srl a
    add a,L2_PAGE
    nextreg 0x51,a
    ld a,(tc_p)
    and 31
    add a,0x20
    ld h,a                      ; HL = 0x2000 + (p & 31) * 256
    ; clear the old ridge
    ld a,b
    or a
    jr z,tc_new
    ld l,a
    ld a,SKY
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    ld (hl),a
tc_new:
    ld a,c
    or a
    jr z,tc_store
    ld l,a                      ; the new ridge
    push bc
    ld de,_tc_ridge
    ex de,hl
    ld bc,7
    ldir
    pop bc
tc_store:
    ld hl,(tc_p)
    ld de,COL_H
    add hl,de
    ld (hl),c
    ld hl,(tc_p)
    add hl,hl
    ld de,COL_WX
    add hl,de
    ld de,(tc_wx)
    ld (hl),e
    inc hl
    ld (hl),d
    ld hl,(_tc_drawn)
    inc hl
    ld (_tc_drawn),hl
tc_next:
    ld hl,(tc_wx)               ; wx = (wx + 1) mod 1920
    inc hl
    ld de,1920
    or a
    sbc hl,de
    jr nc,tc_wx1
    add hl,de
tc_wx1:
    ld (tc_wx),hl
    ld hl,(tc_p)                ; p = (p + 1) mod 320
    inc hl
    ld de,320
    or a
    sbc hl,de
    jr nc,tc_p1
    add hl,de
tc_p1:
    ld (tc_p),hl
    ld hl,(tc_n)
    dec hl
    ld (tc_n),hl
    ld a,h
    or l
    jp nz,tc_loop
tc_done:
    nextreg 0x51,0xFF           ; the ROM back
    pop ix
    ret

SECTION data_user
PUBLIC _tc_ridge
_tc_ridge: defb 0x54, 0x4C, 0x4C, 0x48, 0x48, 0x28, 0x24   ; view.c's ridge colours

SECTION bss_user
_tc_vx:      defw 0
_tc_s0:      defw 0
_tc_s1:      defw 0
_tc_flat:    defb 0
_tc_terrain: defw 0
_tc_drawn:   defw 0
tc_p:        defw 0
tc_wx:       defw 0
tc_n:        defw 0
