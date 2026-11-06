; Boing Pinball's position step on the Z80 (the multiplies are the SDK's
; fixmath.asm).

SECTION code_user

EXTERN fixmath_smul

; void advance(int16_t *p, uint8_t *frac, int16_t v): (p:frac) += v * sub_kpos >> 11
; (pb_phys.c; a substep moves at most 54 P, so the step fits 16 bits)
PUBLIC _advance
EXTERN _sub_kpos
_advance:
    ld hl,6
    add hl,sp
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; HL = v
    ld de,(_sub_kpos)
    call fixmath_smul   ; DEHL = v * kpos
    ; >> 11 = << 5 then the high word: bits 11..26 -> DE
    ld b,5
adv_sh:
    add hl,hl
    rl e
    rl d
    djnz adv_sh                 ; DE = step, 1/256 P
    ld c,d                      ; BC = the step's whole P, sign-extended
    ld a,d
    rla
    sbc a,a
    ld b,a
    ld hl,4
    add hl,sp
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; HL = frac
    ld a,(hl)
    add a,e
    ld (hl),a                   ; frac += the step's fraction
    push af                     ; keep its carry (add hl,sp changes it)
    ld hl,4
    add hl,sp
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; HL = p
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = *p
    pop af
    ex de,hl
    adc hl,bc                   ; *p + step's high byte + the fraction's carry
    ex de,hl
    ld (hl),d
    dec hl
    ld (hl),e
    ret
