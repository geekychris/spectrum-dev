; The physics' multiplies on the Z80N (zmath.c has the C for the host):
; z88dk's l_z80n_muls_32_16x16 (MUL DE), with the shifts done on the bytes.
;   int32_t mul16(int16_t a, int16_t b)    a * b
;   int16_t mulq14(int16_t a, int16_t b)   (a * b) >> 14
;   int16_t mulq8(int16_t a, int16_t b)    (a * b) >> 8

SECTION code_user

PUBLIC _mul16, _mulq14, _mulq8
EXTERN l_z80n_muls_32_16x16

args:                           ; HL = a, DE = b from the caller's stack (called from _mul*)
    ld hl,4
    add hl,sp
    ld a,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld l,a
    ld h,b
    jp l_z80n_muls_32_16x16

_mul16:
    call args                   ; DEHL
    ret

_mulq14:
    call args
    add hl,hl                   ; DEHL << 2, the high word is >> 14
    rl e
    rl d
    add hl,hl
    rl e
    rl d
    ex de,hl
    ret

_mulq8:
    call args
    ld l,h                      ; bits 8..23
    ld h,e
    ret

; uint16_t isqrt32(uint32_t v): bit by bit, all in registers
;   r = 0; for bit = 2^30 .. 1 step /4: if v >= r + bit: v -= r + bit, r = r/2 + bit else r /= 2
; Here the classic "two bits in, one bit out" form: rem:v shifted, 16 rounds.
PUBLIC _isqrt32
_isqrt32:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BCDE = v
    push ix
    ld ix,0                     ; IX = root
    ld hl,0                     ; HL = remainder (17 bits: carry in A bit 0)
    xor a
    exx
    ld b,16
    exx
isq_loop:
    ; remainder = remainder << 2 | top two bits of v
    sla e
    rl d
    rl c
    rl b
    adc hl,hl
    rla
    sla e
    rl d
    rl c
    rl b
    adc hl,hl
    rla
    ; root <<= 1; trial = root << 1 | 1
    add ix,ix
    push bc
    push ix
    pop bc
    sla c
    rl b                        ; BC = 2 * root (17th bit lost: root < 2^15 here)
    push af
    and 1
    ld a,0
    rla                         ; A = 0 (no high bit in the trial)
    pop af
    inc bc                      ; trial = 2 root + 1
    ; if A:HL >= trial: A:HL -= trial, root++
    push hl
    or a
    sbc hl,bc
    jr nc,isq_ge
    or a
    jr z,isq_lt                 ; A = 0 and HL < trial
    dec a                       ; borrow from the 17th bit
isq_ge:
    pop bc                      ; drop the old remainder
    inc ix
    pop bc
    jr isq_next
isq_lt:
    pop hl                      ; keep the old remainder
    pop bc
isq_next:
    exx
    dec b
    exx
    jr nz,isq_loop
    push ix
    pop hl
    pop ix
    ret

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
    call l_z80n_muls_32_16x16   ; DEHL = v * kpos
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
