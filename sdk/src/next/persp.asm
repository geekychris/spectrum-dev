; persp.asm - persp_mul and persp_xy for the Z80N (persp.h)

SECTION code_user

PUBLIC _persp_mul, _persp_xy, _persp_xs, _persp_scale, _persp_x1, _persp_y1
EXTERN _persp_k
EXTERN l_z80n_muls_32_16x16     ; HL * DE -> DEHL (signed)

; int16_t persp_mul(int16_t v, uint16_t s)     (v * s) >> 12, floor; s < 32768
; (saturates at +-32767 if the result needs more than 16 bits)
_persp_mul:
    ld hl,2
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
pmul:                           ; HL = v, DE = s -> HL; uses A, B, DE
    call l_z80n_muls_32_16x16   ; DEHL = v * s
    ; bits 12..27 = (D:E:H) >> 4; it fits if bits 27..31 are all the same
    ld a,d
    sra a
    sra a
    sra a                       ; A = D >> 3 (bits 27..31, sign extended)
    jr z,pm_ok
    inc a
    jr nz,pm_sat
pm_ok:
    ld b,4
pm_sh:
    sra d
    rr e
    rr h
    djnz pm_sh
    ld l,h
    ld h,e
    ret
pm_sat:
    bit 7,d
    ld hl,32767
    ret z
    ld hl,-32767
    ret

; void persp_xy(const int16_t *pts, uint8_t n, int16_t bx, int16_t by, uint16_t s,
;               int16_t cx, int16_t cy, int16_t *out)
; out = cx + persp_mul(x + bx, s), cy - persp_mul(y + by, s), clamped to +-16000
; (x + bx and y + by saturate at 16 bits)
_persp_xy:
    push ix
    ld ix,0
    add ix,sp
    ; (ix+4) pts, (ix+6) n, (ix+7) bx, (ix+9) by, (ix+11) s, (ix+13) cx, (ix+15) cy, (ix+17) out
    ld l,(ix+4)
    ld h,(ix+5)
    ld (px_src),hl
    ld l,(ix+17)
    ld h,(ix+18)
    ld (px_dst),hl
    ld a,(ix+6)
    or a
    jr z,px_done
    ld (px_n),a
px_loop:
    ld hl,(px_src)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld (px_src),hl
    push bc                     ; y
    ; x
    ld l,(ix+7)
    ld h,(ix+8)
    call add_sat                ; HL = x + bx
    ld e,(ix+11)
    ld d,(ix+12)
    call pmul
    ld e,(ix+13)
    ld d,(ix+14)
    call add_clamp              ; HL = cx + HL, clamped
    ex de,hl
    ld hl,(px_dst)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld (px_dst),hl
    ; y
    pop de
    ld l,(ix+9)
    ld h,(ix+10)
    call add_sat                ; HL = y + by
    ld e,(ix+11)
    ld d,(ix+12)
    call pmul
    xor a                       ; HL = -HL
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ld e,(ix+15)
    ld d,(ix+16)
    call add_clamp
    ex de,hl
    ld hl,(px_dst)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld (px_dst),hl
    ld hl,px_n
    dec (hl)
    jr nz,px_loop
px_done:
    pop ix
    ret

; void persp_xs(const int16_t *xs, uint8_t n, int16_t bx, uint16_t s, int16_t cx, int16_t *out)
; out[i] = cx + persp_mul(xs[i] + bx, s), clamped to +-16000
_persp_xs:
    push ix
    ld ix,0
    add ix,sp
    ; (ix+4) xs, (ix+6) n, (ix+7) bx, (ix+9) s, (ix+11) cx, (ix+13) out
    ld l,(ix+4)
    ld h,(ix+5)
    ld (px_src),hl
    ld l,(ix+13)
    ld h,(ix+14)
    ld (px_dst),hl
    ld a,(ix+6)
    or a
    jr z,pxs_done
    ld (px_n),a
pxs_loop:
    ld hl,(px_src)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (px_src),hl
    ld l,(ix+7)
    ld h,(ix+8)
    call add_sat
    ld e,(ix+9)
    ld d,(ix+10)
    call pmul
    ld e,(ix+11)
    ld d,(ix+12)
    call add_clamp
    ex de,hl
    ld hl,(px_dst)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld (px_dst),hl
    ld hl,px_n
    dec (hl)
    jr nz,pxs_loop
pxs_done:
    pop ix
    ret

; uint16_t persp_scale(uint16_t z)      persp_k / z (persp_k < 2^24), 65535 if more
_persp_scale:
    ld hl,2
    add hl,sp
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = z
    ld a,b
    or c
    jr nz,ps_nz
    inc c                       ; z = 0 -> 1
ps_nz:
    ; A:H:L = dividend (24 bits) shifted out into the remainder DE, quotient into A:H:L
    ld hl,(_persp_k)
    ld a,(_persp_k + 2)
    ld de,0
    push ix
    ld ixl,24
ps_loop:
    add hl,hl
    rla
    ex de,hl
    adc hl,hl                   ; remainder << 1 | the bit
    jr c,ps_sub                 ; (17 bits: subtract always works)
    or a
    sbc hl,bc
    jr nc,ps_one
    add hl,bc                   ; too small: restore
    ex de,hl
    jr ps_next
ps_sub:
    or a
    sbc hl,bc
ps_one:
    ex de,hl
    inc l                       ; quotient bit
ps_next:
    dec ixl
    jr nz,ps_loop
    pop ix
    or a
    ret z                       ; quotient < 65536
    ld hl,65535
    ret

; int16_t persp_x1(int16_t x, int16_t bx, uint16_t s, int16_t cx)   cx + persp_mul(x + bx, s), clamped
_persp_x1:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    call add_sat                ; HL = x + bx
    push hl
    ld hl,8
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; s
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; cx
    pop hl
    push bc
    call pmul
    pop de
    jp add_clamp

; int16_t persp_y1(int16_t y, int16_t by, uint16_t s, int16_t cy)   cy - persp_mul(y + by, s), clamped
_persp_y1:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    call add_sat
    push hl
    ld hl,8
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)
    pop hl
    push bc
    call pmul
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    pop de
    jp add_clamp

; HL = HL + DE, saturated at +-32767
add_sat:
    or a
    adc hl,de                   ; (16-bit add doesn't set overflow; adc does)
    ret po
    ; overflow: the sign of DE says which way
    bit 7,d
    ld hl,32767
    ret z
    ld hl,-32767
    ret

; HL = HL + DE (|DE| small), clamped to +-16000
add_clamp:
    or a
    adc hl,de
    jp pe,ac_ovf
    ld de,16000 + 1
    bit 7,h
    jr nz,ac_neg
    push hl
    or a
    sbc hl,de
    pop hl
    ret c
    ld hl,16000
    ret
ac_neg:
    ld de,-16000
    push hl
    or a
    sbc hl,de
    pop hl
    ret nc
    ld hl,-16000
    ret
ac_ovf:
    bit 7,h                     ; wrapped: positive overflow shows as negative
    ld hl,16000
    ret nz
    ld hl,-16000
    ret

SECTION bss_user
px_src: defs 2
px_dst: defs 2
px_n:   defs 1
