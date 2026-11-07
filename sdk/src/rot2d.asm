; fixmath.h rot2d: rotate (and scale) a list of small 2D points, int8 in,
; int8 out, rounded: x' = (x * kcx - y * ksx) / 256, y' = (x * ksy + y * kcy)
; / 256 with the Q8 coefficients rot2d_set() computed. Four shift-and-add
; multiplies a point (~45 T-states per bit of |x| or |y|): ~1,000 T-states a
; point with coordinates up to +-15.

SECTION code_user

PUBLIC _rot2d
PUBLIC _rot2d_kcx, _rot2d_ksx, _rot2d_kcy, _rot2d_ksy

; void rot2d(const int8_t *in, int8_t *out, uint8_t n)
_rot2d:
    push ix
    ld ix,4
    add ix,sp
    ld l,(ix+0)
    ld h,(ix+1)
    ld (r2_in),hl
    ld l,(ix+2)
    ld h,(ix+3)
    ld (r2_out),hl
    ld b,(ix+4)
    pop ix
    ld a,b
    or a
    ret z
r2_point:
    push bc
    ld hl,(r2_in)
    ld a,(hl)
    ld (r2_x),a
    inc hl
    ld a,(hl)
    ld (r2_y),a
    inc hl
    ld (r2_in),hl
    ; x' = x * kcx - y * ksx
    ld a,(r2_x)
    ld de,(_rot2d_kcx)
    call r2_mul
    push hl
    ld a,(r2_y)
    ld de,(_rot2d_ksx)
    call r2_mul
    ex de,hl
    pop hl
    or a
    sbc hl,de
    call r2_round
    ld hl,(r2_out)
    ld (hl),a
    inc hl
    ld (r2_out),hl
    ; y' = x * ksy + y * kcy
    ld a,(r2_x)
    ld de,(_rot2d_ksy)
    call r2_mul
    push hl
    ld a,(r2_y)
    ld de,(_rot2d_kcy)
    call r2_mul
    pop de
    add hl,de
    call r2_round
    ld hl,(r2_out)
    ld (hl),a
    inc hl
    ld (r2_out),hl
    pop bc
    djnz r2_point
    ret

; A = (HL + 128) / 256, rounded down: the high byte
r2_round:
    ld de,128
    add hl,de
    ld a,h
    ret

; HL = A (signed 8 bits) * DE
r2_mul:
    ld hl,0
    or a
    jp p,r2_mloop
    neg                         ; a < 0: |a| times -DE
    push af
    xor a
    sub e
    ld e,a
    sbc a,a
    sub d
    ld d,a
    pop af
r2_mloop:
    or a
    ret z
    srl a
    jr nc,r2_mskip
    add hl,de
r2_mskip:
    sla e
    rl d
    jr r2_mloop

SECTION bss_user

_rot2d_kcx: defw 0
_rot2d_ksx: defw 0
_rot2d_kcy: defw 0
_rot2d_ksy: defw 0
r2_in:      defw 0
r2_out:     defw 0
r2_x:       defb 0
r2_y:       defb 0
