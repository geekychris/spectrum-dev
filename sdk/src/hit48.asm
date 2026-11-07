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
    push hl                     ; this entry
    ; reach = hit_r + r
    ld de,4
    add hl,de
    ld a,(_hit_r)
    add a,(hl)
    ld (reach),a
    pop hl
    push hl
    ; |dx|
    ld de,(_hit_x)
    ld a,e
    sub (hl)
    ld e,a
    inc hl
    ld a,d
    sbc a,(hl)
    inc hl
    call abs_ad                 ; A:E = dx -> E = |dx| if < 256, else CF
    jr c,hs_next
    ld a,(reach)
    cp e
    jr c,hs_next
    jr z,hs_next                ; |dx| >= reach
    ld a,e
    ld (adx),a
    ; |dy|
    ld de,(_hit_y)
    ld a,e
    sub (hl)
    ld e,a
    inc hl
    ld a,d
    sbc a,(hl)
    call abs_ad
    jr c,hs_next
    ld a,(reach)
    cp e
    jr c,hs_next
    jr z,hs_next
    ; dx^2 + dy^2 < reach^2
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
    jr z,hs_next
    jr c,hs_next
    pop hl
    ld l,c
    ld h,0
    ret
hs_next:
    pop hl
    ld de,6
    add hl,de
    inc c
    djnz hs_loop
hs_none:
    ld hl,0xFF
    ret

; A (high), E (low) = a signed 16-bit difference -> E = its magnitude, CF
; set if it is 256 or more
abs_ad:
    bit 7,a
    jr z,ab_pos
    ld d,a
    xor a
    sub e
    ld e,a
    ld a,0
    sbc a,d                     ; A:E = -value
ab_pos:
    or a
    ret z                       ; < 256 (CF clear from OR)
    scf
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
