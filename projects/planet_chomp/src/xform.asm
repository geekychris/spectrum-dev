; xform.asm - the corners of the sphere's lattice through the camera (render.c's
; xf_corners in Z80N assembly; render.c has the same in C for the host).
;
; Each frame xf_setup puts the camera into the code: four rows of three Q7
; coefficients (the horizon test's direction, then the rotation's rows z, x,
; y), each row a run of MULs with its coefficients as immediates. Corner
; directions are stored + 128 (offset binary), so every product is an
; unsigned MUL; for a row (m0, m1, m2) and a direction (d0, d1, d2):
;
;   sum m d = sum MUL(m & 255, d + 128) - 128 sum m - 256 sum(d + 128 where m < 0)
;
; the constant is the row's start value and the last term is a subtraction
; from the high byte for each negative coefficient (patched in or out).
;
; Then per corner: round the back (horizon dot < xf_thr): hidden. Else the
; rotated direction q (Q14), the ground point b = 3/16 q - C (Q8: the
; planet's radius is 12) and the top t = b + 3/256 q (+ 0.75), each
; projected with the scale table (focal * 16384 / z) into a 16-byte Corner
; record (rtab.h) at 0x4000 + 16 i.

SECTION BANK_18

PUBLIC _xf_setup, _xf_corners
EXTERN _xf_m8, _xf_chat, _xf_c, _xf_thr, _rd_recip, _rd_corner8

CN_BASE equ 0x4000
NCORNERS equ 488
NEAR_Z equ 153                  ; Q8(0.6)
CX equ 128
CY equ 96

; ---------------------------------------------------------------- the rows

; Each row block (row_pre, row_z, row_x, row_y in the loop below):
; HL = sum m d (mod 2^16) for the direction in B, C, IXL (offset binary).
;   +1  ld hl,-128 sum m      +4 ld d,m0 / ld e,b / mul / add hl,de
;   +10 ld d,m1 / ld e,c ...  +16 ld d,m2 / ld e,ixl ...
;   +22 ld a,h  +23 sub b|nop nop  +25 sub c|nop nop  +27 sub ixl|nop nop  +29 ld h,a

; ---------------------------------------------------------------- setup

; void xf_setup(void): xf_chat and xf_m8's rows (2 = z, 0 = x, 1 = y) into
; the four row blocks; C (the camera's position in its own frame) as -C
_xf_setup:
    ld hl,_xf_chat
    ld de,row_pre
    call setup_row
    ld hl,_xf_m8 + 6
    ld de,row_z
    call setup_row
    ld hl,_xf_m8
    ld de,row_x
    call setup_row
    ld hl,_xf_m8 + 3
    ld de,row_y
    call setup_row
    ld hl,(_xf_c)
    call neg_hl
    ld (cx_neg),hl
    ld hl,(_xf_c + 2)
    call neg_hl
    ld (cy_neg),hl
    ld hl,(_xf_c + 4)
    call neg_hl
    ld (cz_neg),hl
    ret

; HL -> three int8 coefficients, DE -> a ROW block
setup_row:
    push de
    pop iy                      ; IY = the block
    ld b,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld a,(hl)                   ; B, C, A = m0, m1, m2
    ld (iy+4),b
    ld (iy+10),c
    ld (iy+16),a
    ; the start value: -128 (m0 + m1 + m2) = (m0 + m1 + m2) * -128
    push af
    ld l,a
    call sext_l                 ; HL = m2
    ex de,hl
    ld l,b
    call sext_l
    add hl,de
    ex de,hl
    ld l,c
    call sext_l
    add hl,de                   ; HL = sum
    ld a,l                      ; HL * -128 = -(HL << 7)
    ld l,0
    srl h
    rra
    rr l
    ld h,a                      ; HL = sum << 7 (H:L = bits)
    call neg_hl
    ld (iy+1),l
    ld (iy+2),h
    pop af
    ; the corrections: sub b (0x90), sub c (0x91), sub ixl (0xDD 0x95) for m < 0
    ld (iy+23),0
    ld (iy+24),0
    ld (iy+25),0
    ld (iy+26),0
    ld (iy+27),0
    ld (iy+28),0
    bit 7,b
    jr z,su_1
    ld (iy+23),0x90
su_1:
    bit 7,c
    jr z,su_2
    ld (iy+25),0x91
su_2:
    bit 7,a
    ret z
    ld (iy+27),0xDD
    ld (iy+28),0x95
    ret

; HL = L sign-extended
sext_l:
    ld a,l
    rlca
    sbc a,a
    ld h,a
    ret

neg_hl:
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ret

; ---------------------------------------------------------------- corners

; void xf_corners(void)
_xf_corners:
    push ix
    push iy
    ld hl,_rd_corner8
    ld (src),hl
    ld iy,CN_BASE
    ld hl,NCORNERS
    ld (count),hl
xc_loop:
    ld hl,(src)
    ld b,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld (src),hl
    ld ixl,a
row_pre:
    ld hl,0
    ld d,0
    ld e,b
    mul d,e
    add hl,de
    ld d,0
    ld e,c
    mul d,e
    add hl,de
    ld d,0
    ld e,ixl
    mul d,e
    add hl,de
    ld a,h
    nop
    nop
    nop
    nop
    nop
    nop
    ld h,a
    ld (iy+10),l
    ld (iy+11),h
    ld (iy+12),0                ; hidden until shown
    bit 7,h
    jp nz,xc_next               ; behind the planet
    ld de,(_xf_thr)
    or a
    sbc hl,de
    jp c,xc_next                ; dot < threshold (both positive here)
row_z:
    ld hl,0
    ld d,0
    ld e,b
    mul d,e
    add hl,de
    ld d,0
    ld e,c
    mul d,e
    add hl,de
    ld d,0
    ld e,ixl
    mul d,e
    add hl,de
    ld a,h
    nop
    nop
    nop
    nop
    nop
    nop
    ld h,a
    ld (qz),hl
row_x:
    ld hl,0
    ld d,0
    ld e,b
    mul d,e
    add hl,de
    ld d,0
    ld e,c
    mul d,e
    add hl,de
    ld d,0
    ld e,ixl
    mul d,e
    add hl,de
    ld a,h
    nop
    nop
    nop
    nop
    nop
    nop
    ld h,a
    ld (qx),hl
row_y:
    ld hl,0
    ld d,0
    ld e,b
    mul d,e
    add hl,de
    ld d,0
    ld e,c
    mul d,e
    add hl,de
    ld d,0
    ld e,ixl
    mul d,e
    add hl,de
    ld a,h
    nop
    nop
    nop
    nop
    nop
    nop
    ld h,a
    ld (qy),hl

    ; z: the ground and the top, both in front of the near plane
    ld hl,(qz)
    call q3_split               ; HL = q3 >> 3, DE = q3 >> 7
    ld bc,(cz_neg)
    add hl,bc                   ; bz
    ld (iy+8),l
    ld (iy+9),h
    push hl
    add hl,de                   ; tz
    ex de,hl                    ; DE = tz
    pop hl                      ; HL = bz
    push de
    ld bc,NEAR_Z
    call cmp_signed             ; bz < NEAR ?
    pop de
    jp c,xc_next
    push de
    ex de,hl
    call cmp_signed             ; tz < NEAR ?
    pop de
    jp c,xc_next
    ld l,(iy+8)
    ld h,(iy+9)
    call scale_of
    ld (sb),hl
    ex de,hl
    call scale_of
    ld (st),hl

    ; x
    ld hl,(qx)
    call q3_split
    ld bc,(cx_neg)
    add hl,bc                   ; bx
    push hl
    add hl,de                   ; tx
    ld de,(st)
    call pmul
    ld de,CX
    add hl,de
    call clampc
    ld (iy+4),l
    ld (iy+5),h
    pop hl
    ld de,(sb)
    call pmul
    ld de,CX
    add hl,de
    call clampc
    ld (iy+0),l
    ld (iy+1),h

    ; y (screen y goes down)
    ld hl,(qy)
    call q3_split
    ld bc,(cy_neg)
    add hl,bc                   ; by
    push hl
    add hl,de                   ; ty
    ld de,(st)
    call pmul
    call neg_hl
    ld de,CY
    add hl,de
    call clampc
    ld (iy+6),l
    ld (iy+7),h
    pop hl
    ld de,(sb)
    call pmul
    call neg_hl
    ld de,CY
    add hl,de
    call clampc
    ld (iy+2),l
    ld (iy+3),h
    ld (iy+12),1

xc_next:
    ld de,16
    add iy,de
    ld hl,(count)
    dec hl
    ld (count),hl
    ld a,h
    or l
    jp nz,xc_loop
    pop iy
    pop ix
    ret

; HL = q: q3 = q + q / 2 (floor); HL = q3 >> 3, DE = q3 >> 7 (arithmetic)
q3_split:
    ld d,h
    ld e,l
    sra d
    rr e
    add hl,de                   ; q3 (|q| <= 16384: no overflow)
    ld d,h
    ld e,l
    sla e
    rl d                        ; carry = sign, D = bits 7..14
    sbc a,a
    ld e,d
    ld d,a                      ; DE = q3 >> 7
    sra h
    rr l
    sra h
    rr l
    sra h
    rr l                        ; HL = q3 >> 3
    ret

; carry if HL < BC (signed)
cmp_signed:
    ld a,h
    xor b
    jp m,cs_diff
    or a
    sbc hl,bc
    ret
cs_diff:
    bit 7,h                     ; signs differ: HL < BC if HL is the negative one
    scf
    ret nz
    or a
    ret

; HL = z (Q8, >= NEAR_Z) -> HL = rd_recip[min(z >> 4, 1023)]
scale_of:
    ld a,h
    cp 64
    jr c,so_in
    ld hl,1023 * 16
so_in:
    ld a,l
    and 0xF0
    ld l,a                      ; z & ~15 = 16 i
    srl h
    rr l
    srl h
    rr l
    srl h
    rr l                        ; 2 i
    ld bc,_rd_recip
    add hl,bc
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ret

; HL = (HL * DE) >> 14, HL signed, DE = 0..32767 (the magnitude, then the sign)
pmul:
    bit 7,h
    jr z,umul14
    call neg_hl
    call umul14
    jp neg_hl

; HL = (HL * DE) >> 14, both 0..32767: four MULs; the low product's low
; byte can't reach bit 14
umul14:
    ld b,h                      ; ah
    ld c,d                      ; bh
    ld h,e                      ; keep bl
    ld d,l                      ; D = al, E = bl
    mul d,e                     ; al bl
    ld a,d                      ; its high byte
    ld d,b
    ld e,h
    mul d,e                     ; ah bl
    add a,e
    ld e,a
    jr nc,um_1
    inc d
um_1:                           ; DE = (al bl >> 8) + ah bl
    ex de,hl                    ; D = bl, E = al
    ld d,c
    mul d,e                     ; al bh
    add hl,de                   ; middle sum (< 65536)
    ld d,b
    ld e,c
    mul d,e                     ; ah bh
    xor a
    add hl,hl
    rla
    add hl,hl
    rla
    ld l,h
    ld h,a                      ; middle >> 6
    ex de,hl
    add hl,hl
    add hl,hl                   ; ah bh << 2
    add hl,de
    ret

; clamp HL to -2048..2047
clampc:
    ld a,h
    add a,8
    and 0xF0
    ret z
    bit 7,h
    ld hl,2047
    ret z
    ld hl,-2048
    ret

SECTION BANK_18
src:    defw 0
count:  defw 0
qz:     defw 0
qx:     defw 0
qy:     defw 0
sb:     defw 0
st:     defw 0
cx_neg: defw 0
cy_neg: defw 0
cz_neg: defw 0
