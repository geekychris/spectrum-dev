; 32-bit integer maths for the physics (phys.asm), as the original's C does it
; with a 32-bit long: products keep their low 32 bits, / truncates towards
; zero, >> is arithmetic. Values live in DEHL (DE the high word), as zsdcc
; returns a long. Z80N: MUL d,e (8 x 8 -> 16) and the barrel shifts.
;
; Register use: every routine may change AF, BC, DE, HL; none touches IX, IY
; or the alternate set unless it says so.

SECTION code_user

PUBLIC m_sra4, m_sra12, m_sla4, m_sla12
PUBLIC m_sra32, m_sla32, m_neg32, m_umul16, m_smul16
PUBLIC m_mul32s16, m_mul32, m_div32, m_udiv32, m_isqrt, m_cmp32
PUBLIC m_tmp32, m_mul32x

; ---- shifts ----

; DEHL >>= B (arithmetic), B = 0..31
m_sra32:
    ld a,b
    or a
    ret z
    cp 16
    jr nc,sra_big
    push hl                     ; low
    push de                     ; high
    ld c,b                      ; n
    ld a,16
    sub b
    ld b,a
    bsla de,b                   ; DE = high << (16 - n)
    ex de,hl                    ; HL = the bits that move into the low word
    pop de                      ; DE = high
    ld b,c
    bsra de,b                   ; DE = high >> n (arithmetic): the new high word
    ex (sp),hl                  ; HL = low; (sp) = moved bits
    ex de,hl                    ; DE = low, HL = new high
    bsrl de,b                   ; DE = low >>> n
    pop bc
    ld a,e
    or c
    ld e,a
    ld a,d
    or b
    ld d,a
    ex de,hl                    ; HL = new low, DE = new high
    ret
sra_big:
    sub 16
    ld b,a
    bsra de,b
    ex de,hl
    ld a,h
    rla
    sbc a,a
    ld d,a
    ld e,a
    ret

; DEHL >>= 4 (arithmetic), with the barrel shifter
m_sra4:
    ld b,4
m_sra4_b:
    ld a,e                      ; E's low nibble moves into H's top
    bsra de,b                   ; the new high word
    and 15
    swapnib
    ex de,hl                    ; HL = new high, DE = low
    bsrl de,b
    or d
    ld d,a
    ex de,hl
    ret

; DEHL >>= 12 (arithmetic): a byte, then 4 bits
m_sra12:
    ld l,h
    ld h,e
    ld e,d
    ld a,d
    rla
    sbc a,a
    ld d,a
    ld b,4
    jr m_sra4_b

; DEHL <<= 4
m_sla4:
    ld b,4
m_sla4_b:
    ld a,h                      ; H's top nibble moves into E's bottom
    bsla de,b
    and 0xF0
    swapnib
    or e
    ld e,a
    ex de,hl                    ; DE = low
    bsla de,b
    ex de,hl
    ret

; DEHL <<= 12: a byte, then 4 bits
m_sla12:
    ld d,e
    ld e,h
    ld h,l
    ld l,0
    ld b,4
    jr m_sla4_b

; DEHL <<= B, B = 0..31
m_sla32:
    ld a,b
    or a
    ret z
    cp 16
    jr nc,sla_big
    push de                     ; high
    push hl                     ; low
    ld c,b
    ld a,16
    sub b
    ld b,a
    ex de,hl                    ; DE = low
    bsrl de,b                   ; DE = low >>> (16 - n): bits moving up
    ex de,hl                    ; HL = them
    pop de                      ; DE = low
    ld b,c
    bsla de,b                   ; DE = low << n: the new low word
    ex (sp),hl                  ; HL = high; (sp) = moved bits
    ex de,hl                    ; DE = high, HL = new low
    bsla de,b                   ; DE = high << n
    pop bc
    ld a,e
    or c
    ld e,a
    ld a,d
    or b
    ld d,a
    ret
sla_big:
    sub 16
    ld b,a
    ex de,hl                    ; DE = low
    bsla de,b
    ld hl,0
    ret

; DEHL = -DEHL
m_neg32:
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

; ---- products ----

; DEHL = HL * DE, unsigned 16 x 16 -> 32 (four MULs)
m_umul16:
    ld a,d                      ; y1
    ld c,e                      ; y0
    ld b,h                      ; x1
    ld d,l                      ; x0 (e = y0)
    mul d,e                     ; p00 = x0 y0
    push de
    ld d,b
    ld e,a
    mul d,e                     ; p11 = x1 y1
    push de
    ld d,l
    ld e,a
    mul d,e                     ; p01 = x0 y1
    ex de,hl
    ld d,b
    ld e,c
    mul d,e                     ; p10 = x1 y0
    add hl,de                   ; the middle (17 bits)
    ld a,0
    adc a,a
    ld b,a                      ; its bit 16
    ld c,h                      ; its high byte
    ld a,l                      ; its low byte
    pop de                      ; p11
    pop hl                      ; p00
    add a,h
    ld h,a
    ld a,e
    adc a,c
    ld e,a
    ld a,d
    adc a,b
    ld d,a
    ret

; DEHL = HL * DE, signed 16 x 16 -> 32
m_smul16:
    ld (mu_x),hl
    ld (mu_y),de
    call m_umul16
    ld a,(mu_x + 1)
    or a
    jp p,sm_1
    ex de,hl                    ; high word -= y
    ld bc,(mu_y)
    or a
    sbc hl,bc
    ex de,hl
sm_1:
    ld a,(mu_y + 1)
    or a
    ret p
    ex de,hl                    ; high word -= x
    ld bc,(mu_x)
    or a
    sbc hl,bc
    ex de,hl
    ret

; the low 16 bits of HL * DE (3 MULs); BC kept
lo_mul16:
    push bc
    ld b,h
    ld c,l
    ld a,d                      ; y1
    ld d,c
    push de                     ; d = x0, e = y0
    ld d,a
    ld e,c
    mul d,e                     ; y1 x0 (low byte matters)
    ld a,e
    ld d,b
    pop bc                      ; b = x0, c = y0
    ld e,c
    mul d,e                     ; x1 y0
    add a,e
    ld h,a
    ld d,b
    ld e,c
    mul d,e                     ; x0 y0
    ld l,e
    ld a,h
    add a,d
    ld h,a
    pop bc
    ret

; DEHL = DEHL * BC (BC a signed 16-bit value), low 32 bits: the partial
; products of the bytes that reach bit 31 (7 MULs), then - x << 16 if BC < 0
m_mul32s16:
    ld (ms_x),hl
    ld (ms_x + 2),de
    ld d,l
    ld e,c
    mul d,e                     ; x0 y0
    ld (ms_r),de
    ld d,l
    ld e,b
    mul d,e                     ; x0 y1
    push de
    ld a,(ms_x + 1)
    ld d,a
    ld e,c
    mul d,e                     ; x1 y0
    pop hl
    add hl,de                   ; P1 (17 bits)
    ld a,0
    adc a,a
    ld (ms_c1),a
    ld a,(ms_r + 1)
    add a,l
    ld (ms_r + 1),a             ; r1 += P1 low
    ld a,h
    adc a,0
    ld l,a
    ld a,(ms_c1)
    adc a,0
    ld h,a                      ; HL = (P1 >> 8) + carry: into r2 r3
    push hl
    ld a,(ms_x + 1)
    ld d,a
    ld e,b
    mul d,e                     ; x1 y1
    push de
    ld a,(ms_x + 2)
    ld d,a
    ld e,c
    mul d,e                     ; x2 y0
    pop hl
    add hl,de
    pop de
    add hl,de                   ; r2 r3 so far
    push hl
    ld a,(ms_x + 2)
    ld d,a
    ld e,b
    mul d,e                     ; x2 y1 (its low byte reaches r3)
    ld a,e
    ld (ms_c1),a
    ld a,(ms_x + 3)
    ld d,a
    ld e,c
    mul d,e                     ; x3 y0 (low byte)
    ld a,(ms_c1)
    add a,e
    pop hl
    add a,h
    ld h,a
    bit 7,b
    jr z,m32s_p
    ld de,(ms_x)                ; y < 0: its sign extension adds -x << 16
    or a
    sbc hl,de
m32s_p:
    ex de,hl
    ld hl,(ms_r)
    ret

; DEHL = DEHL * (BC) (a 32-bit value at address BC), low 32 bits
m_mul32:
    ld (mu_x),hl
    ld (mu_x + 2),de
    ld h,b
    ld l,c
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (mu_y),de                ; c
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; d
    ld hl,(mu_x)
    call lo_mul16               ; a d
    ld (mu_t),hl
    ld hl,(mu_x + 2)
    ld de,(mu_y)
    call lo_mul16               ; b c
    ld bc,(mu_t)
    add hl,bc
    ld (mu_t),hl
    ld hl,(mu_x)
    ld de,(mu_y)
    call m_umul16               ; a c
    ex de,hl
    ld bc,(mu_t)
    add hl,bc
    ex de,hl
    ret

; DEHL = DEHL * (BC) for any two 32-bit values (low 32 bits): a 16 x 16
; product when both fit in 16 bits
m_mul32x:
    ld a,h
    rla
    sbc a,a
    cp d
    jp nz,m_mul32
    cp e
    jp nz,m_mul32
    push hl
    ld h,b
    ld l,c
    inc hl
    ld a,(hl)
    rla
    sbc a,a
    inc hl
    cp (hl)
    jr nz,mx_full
    inc hl
    cp (hl)
    jr nz,mx_full
    dec hl
    dec hl
    ld d,(hl)
    dec hl
    ld e,(hl)
    pop hl
    jp m_smul16
mx_full:
    pop hl
    jp m_mul32

; ---- division ----

; DEHL = DEHL / (BC) (32-bit at address BC), C semantics: truncates towards 0.
; The remainder is not kept. Division by 0 gives 0 (never happens in the rules).
m_div32:
    ld a,d
    ld (dv_sign),a              ; sign of the dividend
    or a
    call m,m_neg32
    push hl
    push de
    ld h,b
    ld l,c
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DEBC = divisor
    ld a,(dv_sign)
    xor d
    ld (dv_sign),a              ; sign of the quotient
    ld h,b
    ld l,c
    bit 7,d
    call nz,m_neg32
    ld (dv_d),hl
    ld (dv_d + 2),de
    pop de
    pop hl
    call m_udiv32
    ld a,(dv_sign)
    or a
    ret p
    jp m_neg32

; DEHL = DEHL / (dv_d), unsigned 32-bit (dv_d != 0). Shift-and-subtract with
; the divisor first lined up under the dividend's top bit, so it takes one
; round per quotient bit (13 for phys.c's usual (x << 12) / len).
m_udiv32:
    ld bc,(dv_d + 2)
    ld a,b
    or c
    jr nz,ud_gen
    ld bc,(dv_d)
    ld a,b
    or c
    jp z,ud_zero
    ; a 16-bit divisor BC: high word first (16 / 16), then the low word with
    ; the remainder (the usual long division in two 16-bit digits); a high
    ; word below the divisor is already the remainder of the first digit
    push hl
    ld h,d
    ld l,e
    or a
    sbc hl,bc
    pop hl
    jr nc,ud_two
    call div16r                 ; (DE:HL) / BC with DE < BC: the quotient fits 16 bits
    ld de,0
    ret
ud_two:
    push hl
    ex de,hl                    ; HL = dividend high
    call div16                  ; HL = high / BC, DE = high % BC
    ld (dv_r + 2),hl
    pop hl                      ; low word; remainder DE carries in
    call div16r
    ld de,(dv_r + 2)
    ret

; HL = HL / BC, DE = HL % BC (BC != 0)
div16:
    ld de,0
; HL = (DE * 65536 + HL) / BC, DE = the remainder, for DE < BC
div16r:
    ld a,16
d16r_l:
    add hl,hl                   ; shift the dividend's next bit into DE
    ex de,hl
    adc hl,hl
    jr c,d16r_big               ; carried out of 16 bits: >= 65536 > BC
    sbc hl,bc                   ; carry is clear here
    jr nc,d16r_one
    add hl,bc
    ex de,hl
    dec a
    jr nz,d16r_l
    ret
d16r_big:
    or a
    sbc hl,bc
d16r_one:
    ex de,hl
    inc l                       ; quotient bit (bit 0 is free after the shift)
    dec a
    jr nz,d16r_l
    ret

ud_gen:
    ld (dv_q),hl
    ld (dv_q + 2),de            ; remainder = dividend
    ld hl,(dv_d)
    ld de,(dv_d + 2)
    ld a,d
    or e
    or h
    or l
    jp z,ud_zero
    ; dividend < divisor: 0
    ld bc,(dv_q + 2)
    ex de,hl                    ; HL = divisor high, DE = divisor low
    or a
    sbc hl,bc
    jr c,ud_go                  ; divisor high < dividend high
    jp nz,ud_zero               ; divisor high > dividend high
    ex de,hl
    ld bc,(dv_q)
    or a
    sbc hl,bc
    jr z,ud_go
    jp nc,ud_zero
ud_go:
    ; line up: shift the divisor left while it stays <= the dividend (k rounds)
    ld hl,(dv_d)
    ld (dv_t4),hl
    ld hl,(dv_d + 2)
    ld (dv_t4 + 2),hl
    ld b,1                      ; quotient bits to make
ud_al:
    ld a,(dv_t4 + 3)
    bit 7,a
    jr nz,ud_div                ; can't shift further
    ; next = t4 << 1; stop if next > dividend
    ld hl,(dv_t4)
    ld de,(dv_t4 + 2)
    add hl,hl
    ex de,hl
    adc hl,hl                   ; HL = next high, DE = next low
    ld (dv_n),de
    ld (dv_n + 2),hl
    ld de,(dv_q + 2)
    or a
    sbc hl,de                   ; next high - dividend high
    jr c,ud_take
    jr nz,ud_div
    ld hl,(dv_n)
    ld de,(dv_q)
    or a
    sbc hl,de                   ; next low - dividend low
    jr z,ud_take
    jr nc,ud_div
ud_take:
    ld hl,(dv_n)
    ld (dv_t4),hl
    ld hl,(dv_n + 2)
    ld (dv_t4 + 2),hl
    inc b
    jr ud_al
ud_div:
    ; B rounds: q = q * 2 + (rem >= t4 ? (rem -= t4, 1) : 0); t4 >>= 1
    ld hl,0
    ld (dv_r),hl
    ld (dv_r + 2),hl            ; quotient
ud_l:
    ld hl,(dv_q)
    ld de,(dv_t4)
    or a
    sbc hl,de
    ld (dv_t),hl
    ld hl,(dv_q + 2)
    ld de,(dv_t4 + 2)
    sbc hl,de
    ccf                         ; C = 1 if rem >= t4
    jr nc,ud_0
    ld (dv_q + 2),hl
    ld hl,(dv_t)
    ld (dv_q),hl
ud_0:
    ld hl,(dv_r)                ; quotient = quotient * 2 + C
    adc hl,hl
    ld (dv_r),hl
    ld hl,(dv_r + 2)
    adc hl,hl
    ld (dv_r + 2),hl
    ld hl,dv_t4 + 3             ; t4 >>= 1
    srl (hl)
    dec hl
    rr (hl)
    dec hl
    rr (hl)
    dec hl
    rr (hl)
    djnz ud_l
    ld hl,(dv_r)
    ld de,(dv_r + 2)
    ret
ud_zero:
    ld hl,0
    ld de,0
    ret

; ---- isqrt (phys.c): floor(sqrt(DEHL)) for an unsigned value, in HL (DE = 0) ----
m_isqrt:
    ld a,d
    or e
    or h
    or l
    jr z,isq_zero
    push de
    push hl
    call _isqrt32_fast          ; uint16_t isqrt32_fast(uint32_t), sdcc stack call
    pop bc
    pop bc
    ld de,0
    ret
isq_zero:
    ld de,0
    ret
EXTERN _isqrt32_fast

; ---- compare: signed DEHL against (BC); C if DEHL < (BC), Z if equal ----
m_cmp32:
    push hl
    push de
    ld a,(bc)
    ld (m_tmp32),a
    inc bc
    ld a,(bc)
    ld (m_tmp32 + 1),a
    inc bc
    ld a,(bc)
    ld (m_tmp32 + 2),a
    inc bc
    ld a,(bc)
    ld (m_tmp32 + 3),a
    ; flip the sign bits: then an unsigned compare is a signed one
    ld a,d
    xor 0x80
    ld d,a
    ld a,(m_tmp32 + 3)
    xor 0x80
    ld b,a
    ld a,d
    cp b
    jr nz,cmp_done
    ld a,e
    ld bc,(m_tmp32 + 1)
    cp b
    jr nz,cmp_done
    ld a,h
    cp c
    jr nz,cmp_done
    ld a,(m_tmp32)
    ld b,a
    ld a,l
    cp b
cmp_done:
    pop de
    pop hl
    ret

SECTION bss_user
mu_r:   defs 4
mu_x:   defs 4
mu_y:   defs 4
mu_t:   defs 2
dv_sign: defs 1
dv_d:   defs 4
dv_q:   defs 4
dv_r:   defs 4                  ; must follow dv_q
dv_t:   defs 2
dv_t4:  defs 4
dv_n:   defs 4
m_tmp32: defs 4
ms_x:   defs 4
ms_r:   defs 2
ms_c1:  defs 1
