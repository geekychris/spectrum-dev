; hit48.h: scan a list of circles {x, y (int16), r, id} for the first one
; within reach of (hit_x, hit_y) with radius hit_r. Returns L = its index or
; 0xFF.

SECTION code_user

PUBLIC _hit_scan, _hit_x, _hit_y, _hit_r, _hit_list, _hit_n

_hit_scan:
    ld hl,(_hit_list)
    ld a,(_hit_n)
    or a
    jr z,hs_none
    ld b,a
    ld c,0                      ; index
hs_loop:
    ; |dx| = |hit_x - x|, rejected unless < 256
    ld a,(_hit_x)
    sub (hl)
    ld e,a
    inc hl
    ld a,(_hit_x + 1)
    sbc a,(hl)
    inc hl                      ; HL = y
    jr z,hs_dxok
    inc a
    jr nz,hs_far                ; < -256 or >= 256
    xor a
    sub e
    jr z,hs_far                 ; -256
    ld e,a
hs_dxok:
    ; reach = hit_r + r; reject when |dx| >= reach
    inc hl
    inc hl
    ld a,(_hit_r)
    add a,(hl)
    dec hl
    dec hl
    cp e
    jr c,hs_far
    jr z,hs_far
    ld (reach),a
    ld a,e
    ld (adx),a
    ; |dy|
    ld a,(_hit_y)
    sub (hl)
    ld e,a
    inc hl
    ld a,(_hit_y + 1)
    sbc a,(hl)
    dec hl
    jr z,hs_dyok
    inc a
    jr nz,hs_far
    xor a
    sub e
    jr z,hs_far
    ld e,a
hs_dyok:
    ld a,(reach)
    cp e
    jr c,hs_far
    jr z,hs_far
    ; dx^2 + dy^2 < reach^2
    push hl
    ld a,e
    call square                 ; HL = dy^2
    push hl
    ld a,(adx)
    call square
    pop de
    add hl,de
    push hl
    ld a,(reach)
    call square                 ; HL = reach^2
    pop de                      ; DE = dx^2 + dy^2
    or a
    sbc hl,de                   ; reach^2 - d^2 > 0: hit
    pop hl
    jr z,hs_far
    jr c,hs_far
    ld l,c
    ld h,0
    ret
hs_far:                         ; HL = this entry's y
    ld de,4
    add hl,de
    inc c
    djnz hs_loop
hs_none:
    ld hl,0xFF
    ret

; HL = A * A (A < 128)
square:
    push bc
    ld e,a
    ld d,0
    ld hl,0
    ld b,8
sq_loop:
    add hl,hl
    rla
    jr nc,sq_skip
    add hl,de
sq_skip:
    djnz sq_loop
    pop bc
    ret

SECTION bss_user
_hit_x:    defw 0
_hit_y:    defw 0
_hit_r:    defb 0
_hit_n:    defb 0
_hit_list: defw 0
reach:     defb 0
adx:       defb 0
