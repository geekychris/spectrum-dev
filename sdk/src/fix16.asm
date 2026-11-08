; fix16.h on the Z80: mulu16, fmul and fdiv. The 16 x 16 multiplies are
; z88dk's l_mulu_32_16x16 (the Z80N's MUL on the Next, shift-and-add on a
; 48K): fmul takes one to four of them, by the operands' sizes. Uses the alternate registers (the SDK's interrupt saves them).

SECTION code_user

PUBLIC _mulu16, _fmul, _fdiv
EXTERN l_mulu_32_16x16

; uint32_t mulu16(uint16_t a, uint16_t b)
_mulu16:
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
    jp l_mulu_32_16x16          ; DEHL = a * b


; fix fmul(fix a, fix b): bits 16..47 of the signed 64-bit product, i.e.
; floor(a * b / 65536) mod 2^32. On the magnitudes: P = |a| * |b| from as
; few 16 x 16 products as their sizes need (one when both are below 65536,
; two when one is, four otherwise; none when one is 0 or exactly 65536),
; R = P >> 16 (mod 2^32), and for a negative product -(R + (P's low 16 bits
; != 0)), which is the floor.
;   P = al*bl + (al*bh + ah*bl) << 16 + ah*bh << 32
_fmul:
    ld hl,2
    add hl,sp
    ld de,fm_al
    ld bc,8
    ldir                        ; fm_al fm_ah fm_bl fm_bh
    ld a,(fm_ah+1)
    ld hl,fm_bh+1
    xor (hl)
    ld (fd_sign),a              ; bit 7: the product is negative
    ld hl,fm_al
    call fd_abs                 ; |a|
    ld hl,fm_bl
    call fd_abs                 ; |b|
    ld hl,(fm_ah)
    ld a,h
    or l
    jp nz,fm_abig
    ; |a| < 65536
    ld hl,(fm_al)
    ld a,h
    or l
    jp z,fm_zero
    ld hl,(fm_bh)
    ld a,h
    or l
    jp nz,fm_a16_bbig
    ld hl,(fm_al)               ; both < 65536: P = al * bl
    ld de,(fm_bl)
    call l_mulu_32_16x16        ; DEHL
    ld (fm_acc),hl              ; the fraction
    ex de,hl                    ; HL = R low, DE = R high = 0
    ld de,0
    jp fm_sign
fm_a16_bbig:                    ; P = al * bl + al * bh << 16
    ld a,h                      ; |b| = 65536 exactly: R = |a|
    or a
    jr nz,fm_a16_b
    dec l
    jr nz,fm_a16_b
    ld hl,(fm_bl)
    ld a,h
    or l
    jr nz,fm_a16_b
    ld hl,(fm_al)
    ld de,0
    jp fm_exact
fm_a16_b:
    ld hl,(fm_al)
    ld de,(fm_bh)
    jp fm_two
fm_abig:                        ; |a| >= 65536
    ld hl,(fm_bh)
    ld a,h
    or l
    jp nz,fm_bothbig
    ld hl,(fm_bl)               ; |b| < 65536
    ld a,h
    or l
    jp z,fm_zero
    ld hl,(fm_ah)               ; |a| = 65536 exactly: R = |b|
    ld a,h
    or a
    jr nz,fm_b16_a
    dec l
    jr nz,fm_b16_a
    ld hl,(fm_al)
    ld a,h
    or l
    jr nz,fm_b16_a
    ld hl,(fm_bl)
    ld de,0
    jp fm_exact
fm_b16_a:                       ; P = al * bl + ah * bl << 16
    ld hl,(fm_ah)
    ld de,(fm_bl)
fm_two:                         ; X = HL * DE, at bit 16 of P: R = X + (al * bl) >> 16
    call l_mulu_32_16x16
    ld (fm_t),hl
    ld (fm_t+2),de
    ld hl,(fm_al)
    ld de,(fm_bl)
    call l_mulu_32_16x16        ; DEHL = al * bl
    ld (fm_acc),hl              ; the fraction
    ld hl,(fm_t)
    add hl,de
    ld de,(fm_t+2)
    jp nc,fm_sign
    inc de
    jp fm_sign
fm_bothbig:
    ld a,h                      ; |b| = 65536 exactly: R = |a|
    or a
    jr nz,fm_four
    dec l
    jr nz,fm_four
    ld hl,(fm_bl)
    ld a,h
    or l
    jr nz,fm_four
    ld hl,(fm_al)
    ld de,(fm_ah)
    jp fm_exact
fm_four:                        ; the four products
    ld hl,(fm_al)
    ld de,(fm_bh)
    call l_mulu_32_16x16
    ld (fm_t),hl
    ld (fm_t+2),de              ; al * bh
    ld hl,(fm_ah)
    ld de,(fm_bl)
    call l_mulu_32_16x16        ; + ah * bl
    ld bc,(fm_t)
    add hl,bc
    ld (fm_t),hl
    ld hl,(fm_t+2)
    adc hl,de
    ld (fm_t+2),hl
    ld hl,(fm_ah)
    ld de,(fm_bh)
    call l_mulu_32_16x16        ; + (ah * bh) << 16: its low half
    ld de,(fm_t+2)
    add hl,de
    ld (fm_t+2),hl
    ld hl,(fm_al)
    ld de,(fm_bl)
    call l_mulu_32_16x16        ; + (al * bl) >> 16
    ld (fm_acc),hl              ; the fraction
    ld hl,(fm_t)
    add hl,de
    ld de,(fm_t+2)
    jp nc,fm_sign
    inc de
fm_sign:                        ; DEHL = R; the fraction in fm_acc
    ld a,(fd_sign)
    rla
    ret nc
    ld bc,(fm_acc)
    ld a,b
    or c
    jr z,fm_neg
    inc l                       ; R + 1
    jr nz,fm_neg
    inc h
    jr nz,fm_neg
    inc de
fm_neg:                         ; DEHL = -DEHL
    xor a
    sub l
    ld l,a
    ld a,0
    sbc a,h
    ld h,a
    ld a,0
    sbc a,e
    ld e,a
    ld a,0
    sbc a,d
    ld d,a
    ret
fm_exact:                       ; DEHL = R exactly (no fraction)
    ld a,(fd_sign)
    rla
    ret nc
    jr fm_neg
fm_zero:
    ld hl,0
    ld d,h
    ld e,l
    ret


; fix fdiv(fix a, fix b): ((int64)a << 16) / b, truncated toward zero, low
; 32 bits. Unsigned |a| << 16 / |b| by 48 restoring steps: one 32-bit
; register holds the dividend's bits going out at the top and the
; quotient's coming in at the bottom (the last 16 dividend bits are zeros,
; and only the quotient's low 32 bits are kept).
;   main: HL = remainder low, DE = shift register low, BC = divisor low
;   alt:  HL = remainder high, DE = shift register high, BC = divisor high
_fdiv:
    ld hl,2
    add hl,sp
    ld de,fm_al
    ld bc,8
    ldir                        ; a -> fm_al/fm_ah, b -> fm_bl/fm_bh
    ld a,(fm_ah+1)
    ld hl,fm_bh+1
    xor (hl)
    ld (fd_sign),a              ; bit 7: the signs differ
    ld hl,fm_al
    call fd_abs                 ; |a|
    ld hl,fm_bl
    call fd_abs                 ; |b|
    ld hl,(fm_al)
    ld de,(fm_ah)
    ld a,h
    or l
    or d
    or e
    jp z,fd_zero                ; 0 / b = 0
    exx
    ld de,(fm_ah)               ; shift register = |a|
    ld bc,(fm_bh)
    ld hl,0
    exx
    ld de,(fm_al)
    ld bc,(fm_bl)
    ld hl,0
    ; leading zero bytes of |a|: 8 steps at a time (they only shift)
fd_skip:
    exx
    ld a,d
    exx
    or a
    jr nz,fd_go
    exx                         ; shift register <<= 8
    ld d,e
    ld e,0
    exx
    ld a,d
    exx
    ld e,a
    exx
    ld d,e
    ld e,0
    ld a,(fd_steps)
    sub 8
    ld (fd_steps),a
    jr fd_skip
fd_go:
    ld a,(fd_steps)
    sub 16
    call fd_steps1              ; |a|'s bits
    ld a,16
    call fd_steps0              ; then 16 zeros
    jr fd_end

; A steps taking the remainder's new bit from the shift register's top
fd_steps1:
    ex de,hl                    ; shift register << 1 ...
    add hl,hl
    ex de,hl
    exx
    ex de,hl
    adc hl,hl                   ; ... its top bit in the carry
    ex de,hl
    exx
    adc hl,hl                   ; remainder << 1 | that bit
    call fd_step
    dec a
    jr nz,fd_steps1
    ret

; A steps shifting a zero into the remainder (the shift register's top
; bit, a quotient bit past the 32 kept, is dropped)
fd_steps0:
    ex de,hl
    add hl,hl
    ex de,hl
    exx
    ex de,hl
    adc hl,hl
    ex de,hl
    exx
    add hl,hl                   ; remainder << 1
    call fd_step
    dec a
    jr nz,fd_steps0
    ret

fd_step:                        ; the remainder's high half, compare, subtract
    exx
    adc hl,hl
    jr c,fd_force               ; remainder >= 2^32 > divisor
    exx
    or a
    sbc hl,bc                   ; remainder - divisor
    exx
    sbc hl,bc
    jr nc,fd_one
    exx                         ; borrow: put it back
    add hl,bc
    exx
    adc hl,bc
    exx
    ret
fd_force:
    exx
    or a
    sbc hl,bc
    exx
    sbc hl,bc
fd_one:
    exx
    inc e                       ; quotient bit 1
    ret
fd_end:
    exx                         ; DE' DE = quotient
    push de
    exx
    ex de,hl                    ; HL = low
    pop de                      ; DE = high
    ld a,48
    ld (fd_steps),a
    ld a,(fd_sign)
    rla
    ret nc
    ; negate DEHL
    xor a
    sub l
    ld l,a
    ld a,0
    sbc a,h
    ld h,a
    ld a,0
    sbc a,e
    ld e,a
    ld a,0
    sbc a,d
    ld d,a
    ret
fd_zero:
    ld hl,0
    ld d,h
    ld e,l
    ret

fd_abs:                         ; (HL) 32 bits: negate if negative
    push hl
    inc hl
    inc hl
    inc hl
    bit 7,(hl)
    pop hl
    ret z
    xor a
    ld b,4
fd_neg:
    ld a,0
    sbc a,(hl)
    ld (hl),a
    inc hl
    djnz fd_neg
    ret

SECTION data_user
fd_steps: defb 48

SECTION bss_user
fm_al:  defw 0
fm_ah:  defw 0
fm_bl:  defw 0
fm_bh:  defw 0
fm_acc: defw 0, 0
fm_t:   defw 0, 0
fd_sign: defb 0
