; fixmath.h muls(a, b): a * b (low 16 bits) by shift-and-add over the bits
; of |a|: ~40 T-states a bit, so a small factor (|a| < 16, e.g. a vertex
; coordinate times a Q8 sine) costs ~250 T-states with the call, against
; ~1,000 for a general 16 x 16 multiply.

SECTION code_user

PUBLIC _muls

; int16_t muls(int16_t a, int16_t b)
_muls:
    ld hl,2
    add hl,sp
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = a
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = b
    bit 7,b
    jr z,muls_pos
    xor a                       ; a < 0: a = -a, b = -b
    sub c
    ld c,a
    sbc a,a
    sub b
    ld b,a
    xor a
    sub e
    ld e,a
    sbc a,a
    sub d
    ld d,a
muls_pos:
    ld hl,0
muls_loop:
    ld a,b
    or c
    ret z
    srl b
    rr c
    jr nc,muls_skip
    add hl,de
muls_skip:
    ex de,hl
    add hl,hl
    ex de,hl
    jr muls_loop
