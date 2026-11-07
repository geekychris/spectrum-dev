; fixmath.h on the Z80: 16 x 16 multiplies through z88dk's l_mulu_32_16x16
; (the Z80N's MUL in the Next's library, a shift-and-add loop on a 48K), the shifts done on the bytes, and
; a register-only 32-bit square root.

SECTION code_user

PUBLIC _mul16, _mulq14, _mulq8, _isqrt32
EXTERN l_mulu_32_16x16

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
    jp fixmath_smul


; DEHL = HL * DE, signed: the unsigned product (z88dk's l_mulu_32_16x16:
; MUL on the Z80N) minus the other operand from the high word for each
; negative one
PUBLIC fixmath_smul
fixmath_smul:
    push hl                     ; a
    push de                     ; b
    call l_mulu_32_16x16        ; DEHL = unsigned a * b
    pop bc                      ; BC = b
    ex (sp),hl                  ; HL = a, the low word on the stack
    bit 7,h
    jr z,smul_b
    ex de,hl                    ; HL = high, DE = a
    or a
    sbc hl,bc                   ; a < 0: high -= b
    ex de,hl
smul_b:
    bit 7,b
    jr z,smul_done
    ex de,hl                    ; HL = high, DE = a
    or a
    sbc hl,de                   ; b < 0: high -= a
    ex de,hl
smul_done:
    pop hl                      ; the low word
    ret

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
    ; the last round's trial (2 root + 1) needs 17 bits once root reaches
    ; 2^15, and BC keeps 16: the root can come out one too big there (never
    ; too small). So: if root * root > v, root - 1.
    push hl                     ; SP: root, return, v
    ld d,h
    ld e,l
    call l_mulu_32_16x16        ; DEHL = root * root
    ld b,h
    ld c,l
    ld hl,4
    add hl,sp                   ; -> v
    ld a,(hl)
    sub c
    inc hl
    ld a,(hl)
    sbc a,b
    inc hl
    ld a,(hl)
    sbc a,e
    inc hl
    ld a,(hl)
    sbc a,d                     ; carry: v < root * root
    pop hl
    ret nc
    dec hl
    ret

