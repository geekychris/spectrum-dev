; persp.asm - persp_mul and persp_xy for the Z80N (persp.h)

SECTION code_user

PUBLIC _persp_mul, _persp_xy, _persp_xs, _persp_ys, _persp_scale, _persp_x1, _persp_y1
EXTERN _persp_k

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
pmul:                           ; HL = v, DE = s -> HL; uses A, BC, DE
    ; |v| x s by four 8 x 8 MULs (s < 32768, so |v| x s < 2^31); then >> 12,
    ; floor: a negative v gives -ceil(|v| x s / 4096)
    ld a,h
    ld (pm_neg),a               ; bit 7: v < 0
    rla
    jr nc,pm_pos
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a                      ; HL = |v| (32768 for -32768)
pm_pos:
    ld b,h
    ld c,l                      ; BC = |v|
    ld h,d
    ld l,e                      ; HL = s
    ld d,b
    ld e,l
    mul d,e                     ; vh x sl
    push de
    ld d,c
    ld e,h
    mul d,e                     ; vl x sh
    push de
    ld d,b
    ld e,h
    mul d,e                     ; vh x sh: bits 16..31
    ld a,d
    ld b,e
    ld d,c
    ld e,l
    mul d,e                     ; vl x sl: bits 0..15
    ld c,e                      ; C = bits 0..7
    ld l,d
    ld h,b                      ; A:H:L = Q = the product >> 8 (so far)
    pop de
    add hl,de
    adc a,0
    pop de
    add hl,de
    adc a,0                     ; A:H:L = Q
    ld b,a
    ld a,(pm_neg)
    rla
    ld a,b
    jr nc,pm_shift
    ld de,15                    ; negative: ceil = (Q + 15 + (bits 0..7 != 0)) >> 4
    inc c
    dec c
    jr z,pm_r0
    inc e
pm_r0:
    add hl,de
    adc a,0
pm_shift:
    cp 8
    jr nc,pm_big                ; >= 32768 x 16
    srl a
    rr h
    rr l
    srl a
    rr h
    rr l
    srl a
    rr h
    rr l
    srl a
    rr h
    rr l                        ; HL = |result| < 32768
    ld a,(pm_neg)
    rla
    ret nc
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ret
pm_big:
    ld b,a
    ld a,(pm_neg)
    rla
    jr c,pm_bign
    ld hl,32767
    ret
pm_bign:
    ld a,b                      ; |result| = 32768 exactly: -32768 fits
    cp 8
    jr nz,pm_nsat
    ld a,h
    or a
    jr nz,pm_nsat
    ld a,l
    cp 16
    jr nc,pm_nsat
    ld hl,-32768
    ret
pm_nsat:
    ld hl,-32767
    ret

; pmul16: HL = v, DE = s << 4 (s < 4096) -> HL = (v * s) >> 12 as pmul: the
; product's top 16 bits, no shifting (persp_xs/ys use it when s is small)
pmul16:
    ld a,h
    ld (pm_neg),a
    rla
    jr nc,p16_pos
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
p16_pos:
    ld b,h
    ld c,l                      ; BC = |v|
    ld h,d
    ld l,e                      ; HL = s << 4
    ld d,b
    ld e,h
    mul d,e                     ; vh x sh: bits 16..31
    push de
    ld d,b
    ld e,l
    mul d,e                     ; vh x sl
    push de
    ld d,c
    ld e,h
    mul d,e                     ; vl x sh
    push de
    ld d,c
    ld e,l
    mul d,e                     ; vl x sl
    ld a,e                      ; bits 0..7
    ld l,d
    ld h,0                      ; bits 8..
    pop de
    add hl,de
    ld c,0
    rl c
    pop de
    add hl,de
    jr nc,p16_mid
    inc c                       ; C:H:L = the middle (bits 8..25)
p16_mid:
    or l
    ld b,a                      ; B != 0: bits 0..15 not all zero
    ld e,h
    ld d,c
    pop hl
    add hl,de                   ; HL = bits 16..31
    jr c,p16_big
    ld a,(pm_neg)
    rla
    jr c,p16_neg
    bit 7,h
    ret z
    ld hl,32767
    ret
p16_neg:
    ld a,b                      ; ceil
    or a
    jr z,p16_n0
    inc hl
p16_n0:
    ld a,h
    cp 0x80
    jr c,p16_nok
    jr nz,p16_nsat
    ld a,l
    or a
    jr nz,p16_nsat              ; (32768 exactly: -32768 fits)
p16_nok:
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ret
p16_big:
    ld a,(pm_neg)
    rla
    ld hl,32767
    ret nc
p16_nsat:
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
    xor a
    jr pb_args

; void persp_ys(const int16_t *ys, uint8_t n, int16_t by, uint16_t s, int16_t cy, int16_t *out)
; out[i] = cy - persp_mul(ys[i] + by, s), clamped to +-16000 (persp_y1 for n heights)
_persp_ys:
    ld a,1
pb_args:
    ld (px_neg),a
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (px_src),de
    ld a,(hl)
    inc hl
    or a
    ret z
    ld (px_n),a
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (px_b),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl                      ; DE = s
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld (px_c),bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    ld (px_dst),bc
    ld hl,pmul
    ld a,d
    cp 0x10
    jr nc,pb_slow               ; s >= 4096
    ex de,hl                    ; s < 4096: s << 4 for pmul16
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ex de,hl
    ld hl,pmul16
pb_slow:
    ld (px_s),de
    ld (pb_call + 1),hl
pb_loop:
    ld hl,(px_src)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (px_src),hl
    ld hl,(px_b)
    call add_sat                ; HL = v + b
    ld de,(px_s)
pb_call:
    call pmul                   ; (pmul or pmul16: set above)
    ld a,(px_neg)
    or a
    jr z,pb_pos
    xor a                       ; y: HL = -HL
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
pb_pos:
    ld de,(px_c)
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
    jr nz,pb_loop
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
    ; the top byte below z (z > 255, or a small fov): it is the first remainder,
    ; and only the low 16 bits need shifting through (16 rounds, not 24)
    ld e,a
    ld a,b
    or a
    jr nz,ps_short
    ld a,e
    cp c
    jr nc,ps_long
ps_short:
    ld ixl,16                   ; DE = remainder (the top byte), HL = the rest
ps_s16:
    add hl,hl
    ex de,hl
    adc hl,hl
    jr c,ps_s_sub
    or a
    sbc hl,bc
    jr nc,ps_s_one
    add hl,bc
    ex de,hl
    jr ps_s_next
ps_s_sub:
    or a
    sbc hl,bc
ps_s_one:
    ex de,hl
    inc l
ps_s_next:
    dec ixl
    jr nz,ps_s16
    pop ix
    ret                         ; HL = the quotient (< 65536: the top byte was below z)
ps_long:
    ld a,e
    ld e,0
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
pm_neg: defs 1                  ; pmul: bit 7 = the sign of v
px_b:   defs 2                  ; persp_xs/ys: bx or by, s (or s << 4), cx or cy, x or y
px_s:   defs 2
px_c:   defs 2
px_neg: defs 1
