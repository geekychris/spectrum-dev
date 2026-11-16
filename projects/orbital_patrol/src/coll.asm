; Orbital Patrol: the collision tests of game.c (check_collision) as a scan
; in assembly. zsdcc's 32-bit C took ~2,500 T-states a pair (the 8.8 world
; positions are 32-bit); this takes ~250.
;
;   int8_t hit_scan(void)
;
; finds the first active entity in a list (hs_list, hs_n of them, hs_stride
; bytes apart: wx at +0, wy at +4 (32-bit, 8.8), a 16-bit active flag at
; +hs_act) whose box overlaps the one at (hs_ax, hs_ay): returns its index,
; or -1. As check_collision: dx = world_dist(ax, bx) (the shortest way round
; the 1920-pixel world), dy = ay - by, overlap when |dx| < hs_hw and
; |dy| < hs_hh (TO_FP of the half sizes).
;
; The world's coordinates are 0..491519 (8.8) and its y below 65536, so the
; differences fit in 24 bits: the top byte of each 32-bit value is ignored.

SECTION code_user

PUBLIC _hit_scan
PUBLIC _hs_ax, _hs_ay, _hs_list, _hs_n, _hs_stride, _hs_act, _hs_hw, _hs_hh

DEFC HALF_LO = 0xC000           ; TO_FP(WORLD_W) / 2 = 0x03C000
DEFC HALF_HI = 0x03
DEFC WORLD_LO = 0x8000          ; TO_FP(WORLD_W) = 0x078000
DEFC WORLD_HI = 0x07

_hit_scan:
    push ix
    ld a,(_hs_act)              ; the active flag's offset into the two
    ld (hs_act1 + 2),a          ; ld a,(ix+d) / or (ix+d) below
    inc a
    ld (hs_act2 + 2),a
    ld ix,(_hs_list)
    ld a,(_hs_n)
    ld l,0xFF
    or a
    jp z,hs_none
    ld b,a
    ld c,0                      ; C = index
hs_loop:
    push bc
hs_act1:
    ld a,(ix+0)                 ; active? (the displacements are patched)
hs_act2:
    or (ix+0)
    jp z,hs_next
    ; |dy| < hh first: it rejects most pairs (dy = ay - by, no wrap)
    ld hl,(_hs_ay)
    ld e,(ix+4)
    ld d,(ix+5)
    or a
    sbc hl,de
    ld a,(_hs_ay + 2)
    sbc a,(ix+6)
    call abs24
    ld de,(_hs_hh)
    or a
    sbc hl,de
    ld e,a
    ld a,(_hs_hh + 2)
    ld d,a
    ld a,e
    sbc a,d
    jp nc,hs_next
    ; dx = ax - bx (24 bits in A:HL)
    ld hl,(_hs_ax)
    ld e,(ix+0)
    ld d,(ix+1)
    or a
    sbc hl,de
    ld a,(_hs_ax + 2)
    sbc a,(ix+2)
    ; world_dist: if dx > half, dx -= world; if dx < -half, dx += world
    bit 7,a
    jr nz,hs_dx_neg
    cp HALF_HI                  ; dx > 0x03C000 ?
    jr c,hs_dx_ok
    jr nz,hs_dx_wrap_down
    ld de,HALF_LO + 1           ; A = 3: HL > 0xC000 ?
    push hl
    or a
    sbc hl,de
    pop hl
    jr c,hs_dx_ok
hs_dx_wrap_down:
    ld de,WORLD_LO
    or a
    sbc hl,de
    sbc a,WORLD_HI
    jr hs_dx_ok
hs_dx_neg:                      ; dx < -0x03C000 = 0xFC4000 ?
    cp 0x100 - HALF_HI - 1      ; A < 0xFC: below
    jr c,hs_dx_wrap_up
    jr nz,hs_dx_ok              ; A > 0xFC (A = 0xFD..0xFF): above
    ld de,0x4000                ; A = 0xFC: HL < 0x4000 ?
    push hl
    or a
    sbc hl,de
    pop hl
    jr nc,hs_dx_ok
hs_dx_wrap_up:
    ld de,WORLD_LO
    add hl,de
    adc a,WORLD_HI
hs_dx_ok:
    ; |dx| < hw ?
    call abs24
    ld de,(_hs_hw)
    or a
    sbc hl,de
    ld e,a
    ld a,(_hs_hw + 2)
    ld d,a
    ld a,e
    sbc a,d
    jr nc,hs_next               ; |dx| >= hw
    ; a hit
    pop bc
    ld l,c
    pop ix
    ret
hs_next:
    ld a,(_hs_stride)
    ld e,a
    ld d,0
    add ix,de
    pop bc
    inc c
    dec b
    jp nz,hs_loop
    ld l,0xFF
hs_none:
    pop ix
    ret

; int16_t world_sx(const LONG *wx) __z88dk_fastcall: world_to_screen_x(*wx,
; ws_vx) (game.c): the shortest way round from the viewport, in pixels
; (FROM_FP: rounded down). 24 bits as above.
PUBLIC _world_sx, _ws_vx
_world_sx:
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    ex de,hl                    ; A:HL = wx
    ld de,(_ws_vx)
    or a
    sbc hl,de
    ld e,a
    ld a,(_ws_vx + 2)
    ld d,a
    ld a,e
    sbc a,d                     ; A:HL = wx - vx
    bit 7,a
    jr nz,ws_neg
    cp HALF_HI
    jr c,ws_ok
    jr nz,ws_down
    ld de,HALF_LO + 1
    push hl
    or a
    sbc hl,de
    pop hl
    jr c,ws_ok
ws_down:
    ld de,WORLD_LO
    or a
    sbc hl,de
    sbc a,WORLD_HI
    jr ws_ok
ws_neg:
    cp 0x100 - HALF_HI - 1
    jr c,ws_up
    jr nz,ws_ok
    ld de,0x4000
    push hl
    or a
    sbc hl,de
    pop hl
    jr nc,ws_ok
ws_up:
    ld de,WORLD_LO
    add hl,de
    adc a,WORLD_HI
ws_ok:
    ld l,h                      ; >> 8: A:H
    ld h,a
    ret

; A:HL = |A:HL| (24-bit two's complement)
abs24:
    bit 7,a
    ret z
    ld e,a
    xor a
    sub l
    ld l,a
    ld a,0
    sbc a,h
    ld h,a
    ld a,0
    sbc a,e
    ret

SECTION bss_user
_hs_ax:     defs 4
_hs_ay:     defs 4
_hs_hw:     defs 4
_hs_hh:     defs 4
_hs_list:   defw 0
_hs_n:      defb 0
_hs_stride: defb 0
_hs_act:    defb 0
_ws_vx:     defs 4
