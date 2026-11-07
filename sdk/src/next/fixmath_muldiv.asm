; fixmath.h muldiv16 (Z80N: MUL)

SECTION code_user

PUBLIC _muldiv16

; int16_t muldiv16(int16_t a, int16_t b, int16_t c) (fixmath.h): a * b / c,
; truncated towards 0 as C's (int32_t)a * b / c, for results that fit 16
; bits (e.g. clipping a line). |a b| from four MULs, then a 16-step
; division (the quotient fits 16 bits, so |a b| >> 16 < |c|).
_muldiv16:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; DE = a, BC = b, HL = c
    ld a,d
    xor b
    xor h
    ex af,af'                   ; the result's sign
    bit 7,h
    jr z,md_c
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
md_c:
    ld (md_div),hl
    ex de,hl
    ld d,b
    ld e,c                      ; HL = a, DE = b
    bit 7,h
    jr z,md_a
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
md_a:
    bit 7,d
    jr z,md_b
    xor a
    sub e
    ld e,a
    sbc a,a
    sub d
    ld d,a
md_b:
    ; P = HL * DE -> md_p (32 bits, little endian)
    push de
    push hl
    ld d,l
    mul d,e                     ; l e
    ld (md_p),de
    pop hl
    pop de
    push de
    push hl
    ld d,h
    mul d,e                     ; h e
    ex de,hl
    pop de                      ; D = h, E = l
    pop bc                      ; B = d, C = e
    push de
    ld d,e
    ld e,b
    mul d,e                     ; l d
    add hl,de
    ld a,0
    adc a,a
    ld e,a                      ; the middle sum's carry
    ld a,(md_p + 1)
    add a,l
    ld (md_p + 1),a
    ld a,h
    adc a,0
    ld l,a
    ld a,e
    adc a,0
    ld h,a
    pop de                      ; D = h
    ld e,b
    mul d,e                     ; h d
    add hl,de
    ld (md_p + 2),hl
    ; Q = P / div: the quotient fits 16 bits (so P's high word < div): 16
    ; steps over the low word, the remainder in HL, the quotient into DE
    ld bc,(md_div)
    ld hl,(md_p + 2)
    ld de,(md_p)
    ld a,16
md_loop:
    sla e
    rl d
    adc hl,hl
    jr c,md_sub                 ; 17 bits: certainly >= div
    or a
    sbc hl,bc
    jr nc,md_one
    add hl,bc
    dec a
    jr nz,md_loop
    jr md_done
md_sub:
    or a
    sbc hl,bc
md_one:
    inc e
    dec a
    jr nz,md_loop
md_done:
    ld (md_p),de
    ld hl,(md_p)
    ex af,af'
    bit 7,a
    ret z
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ret

SECTION bss_user
md_p:     defs 4
md_div:   defw 0
