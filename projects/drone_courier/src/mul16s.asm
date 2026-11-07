; int32_t mul16s(int16_t a, int16_t b): the signed 32-bit product, from
; z88dk's 16 x 16 -> 32 (the Z80N's MUL) less the sign corrections
; (view.c's camera transform: a few hundred T-states against ~1,500 for
; zsdcc's 32 x 32 multiply).

SECTION code_user
PUBLIC _mul16s
EXTERN l_mulu_32_16x16

_mul16s:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = a
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; HL = b
    ld (ms_a),de
    ld (ms_b),hl
    call l_mulu_32_16x16        ; DEHL = a * b, unsigned
    ld a,(ms_a+1)
    or a
    jp p,ms_apos
    ex de,hl                    ; a < 0: the high word less b
    ld bc,(ms_b)
    or a
    sbc hl,bc
    ex de,hl
ms_apos:
    ld a,(ms_b+1)
    or a
    ret p
    ex de,hl                    ; b < 0: the high word less a
    ld bc,(ms_a)
    or a
    sbc hl,bc
    ex de,hl
    ret

SECTION bss_user
ms_a: defs 2
ms_b: defs 2
