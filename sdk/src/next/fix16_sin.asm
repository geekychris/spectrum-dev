; fix16_sin.asm - fix16.h's a3_sin and a3_cos on the Next (two Z80N MULs;
; fix16.c's C elsewhere).

SECTION code_user

PUBLIC _a3_sin, _a3_cos
EXTERN _a3_sin_tab

; int32_t a3_sin(uint16_t a), a3_cos(uint16_t a) (= a3_sin(a + 16384)): the
; engine's table interpolated, s0 + ((s1 - s0) * f) >> 8 with i, f the
; angle's high and low bytes, s0, s1 = a3_sin_tab[i], [i + 1 mod 256]
; (fix16.c's C on the host). The neighbours differ by at most 1608: the
; product is |s1 - s0| * f from two MULs, the shift floored for a negative
; difference.
_a3_cos:
    pop bc
    pop hl
    push hl
    push bc
    ld de,16384
    add hl,de
    jr as_go
_a3_sin:
    pop bc
    pop hl
    push hl
    push bc
as_go:                          ; HL = the angle: H = i, L = f
    ld a,l
    ld (as_f),a
    ld b,h
    ld l,h
    ld h,0
    add hl,hl
    add hl,hl
    ld de,_a3_sin_tab
    add hl,de                   ; &tab[i]
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (as_s0),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (as_s0 + 2),de
    inc b                       ; i + 1 wraps to the table's start
    jr nz,as_nx
    ld hl,_a3_sin_tab
as_nx:
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; s1's low word
    ld de,(as_s0)
    or a
    sbc hl,de                   ; d = s1 - s0 (16 bits)
    ld a,h
    ld (as_sg),a
    bit 7,h
    jr z,as_dp
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a                      ; |d|
as_dp:
    ld a,(as_f)
    ld e,a
    ld d,l
    mul d,e                     ; |d| low * f
    ld b,e                      ; P's bits 0..7
    ld c,d
    ld e,a
    ld d,h
    mul d,e                     ; |d| high * f
    ld l,c
    ld h,0
    add hl,de                   ; HL = P >> 8 (P < 2^19)
    ld a,(as_sg)
    rla
    jr nc,as_add
    ld a,b                      ; d < 0: floor(-P / 256) = -(P >> 8) - (P & 255 != 0)
    or a
    jr z,as_ng
    inc hl
as_ng:
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
as_add:                         ; DEHL = s0 + HL (sign-extended)
    ex de,hl
    ld a,d
    rla
    sbc a,a
    ld c,a
    ld b,a
    ld hl,(as_s0)
    add hl,de
    ex de,hl
    ld hl,(as_s0 + 2)
    adc hl,bc
    ex de,hl
    ret

SECTION bss_user
as_s0:  defw 0, 0
as_f:   defb 0
as_sg:  defb 0
