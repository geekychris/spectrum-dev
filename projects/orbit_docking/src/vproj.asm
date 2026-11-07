; vproj.asm - view.c's corner projection for one plane, in Z80N assembly:
;
;   for i in 0..n-1:  sx[i] = c + ((e * y[i] - f * x[i]) >> 11)
;                     sy[i] = d - ((e * x[i] + f * y[i]) >> 12)   (port: half height)
;
; e, f the plane's coefficients (k cos, k sin in Q11), c, d its offsets
; (the screen's middle included). The corners come as records made once
; (vproj_rec): |x| (2 bytes), |y| (2 bytes), signs (bit 0 x < 0, bit 1
; y < 0); |x|, |y| < 512, so a product is |coefficient| x |corner| in two
; MULs (the corner's high byte is 0 or 1), kept in 24 bits: view.c only
; calls this for a plane whose products and their sums stay within +-2^23.
; The shifts are arithmetic (floor), as C's on an int32.
;
; void vproj(void): arguments in the variables below (view.c sets them)

SECTION code_user

PUBLIC _vproj
PUBLIC _pj_e, _pj_f, _pj_c, _pj_d, _pj_rec, _pj_sx, _pj_sy, _pj_n

; A:H:L = |BC| * (D:E) (|BC| < 32768, D = 0 or 1); keeps BC
umul:
    ld l,e                      ; vl
    ld h,d                      ; vh
    ld d,c
    mul d,e                     ; cl * vl
    push de
    ld d,b
    ld e,l
    mul d,e                     ; ch * vl
    ld a,h                      ; vh
    pop hl                      ; HL = cl * vl
    push af
    ld a,h
    add a,e
    ld h,a
    ld a,d
    adc a,0
    ld e,a                      ; A:H:L would be E:H:L
    pop af
    or a
    ld a,e
    ret z
    ld e,a                      ; + |c| << 8
    ld a,h
    add a,c
    ld h,a
    ld a,e
    adc a,b
    ret

; A:H:L = -A:H:L (uses E)
neg24:
    ld e,a
    xor a
    sub l
    ld l,a
    ld a,0
    sbc a,h
    ld h,a
    ld a,0
    sbc a,e
    ret

_vproj:
    push ix
    push iy
    ; |e|, |f| and their signs
    ld hl,(_pj_e)
    ld a,h
    rla
    sbc a,a
    ld (se),a                   ; 0 or 0xFF
    bit 7,h
    call nz,neg_hl
    ld (ae),hl
    ld hl,(_pj_f)
    ld a,h
    rla
    sbc a,a
    ld (sf),a
    bit 7,h
    call nz,neg_hl
    ld (af_),hl
    ld iy,(_pj_rec)
    ld ix,(_pj_sx)
vp_loop:
    ld a,(_pj_n)
    or a
    jp z,vp_done
    dec a
    ld (_pj_n),a
    ld a,(iy+4)                 ; the signs: 0xFF masks
    rra
    sbc a,a
    ld (sx_),a
    ld a,(iy+4)
    rra
    rra
    sbc a,a
    ld (sy_),a

    ; e |y|, signed by se ^ sy
    ld bc,(ae)
    ld e,(iy+2)
    ld d,(iy+3)
    call umul
    ld e,a
    ld a,(se)
    ld d,a
    ld a,(sy_)
    xor d
    ld a,e
    call nz,neg24
    ld (p0),hl
    ld (p0 + 2),a
    ; f |x|, signed by sf ^ sx, subtracted
    ld bc,(af_)
    ld e,(iy+0)
    ld d,(iy+1)
    call umul
    ld e,a
    ld a,(sf)
    ld d,a
    ld a,(sx_)
    xor d
    ld a,e
    call nz,neg24
    ld e,a                      ; p0 - A:H:L
    ld a,(p0)
    sub l
    ld l,a
    ld a,(p0 + 1)
    sbc a,h
    ld h,a
    ld a,(p0 + 2)
    sbc a,e
    call shr11
    ld de,(_pj_c)
    add hl,de
    ld (ix+0),l
    ld (ix+1),h

    ; e |x|, signed by se ^ sx
    ld bc,(ae)
    ld e,(iy+0)
    ld d,(iy+1)
    call umul
    ld e,a
    ld a,(se)
    ld d,a
    ld a,(sx_)
    xor d
    ld a,e
    call nz,neg24
    ld (p0),hl
    ld (p0 + 2),a
    ; f |y|, signed by sf ^ sy, added
    ld bc,(af_)
    ld e,(iy+2)
    ld d,(iy+3)
    call umul
    ld e,a
    ld a,(sf)
    ld d,a
    ld a,(sy_)
    xor d
    ld a,e
    call nz,neg24
    ld e,a
    ld a,(p0)
    add a,l
    ld l,a
    ld a,(p0 + 1)
    adc a,h
    ld h,a
    ld a,(p0 + 2)
    adc a,e
    call shr12
    ex de,hl
    ld hl,(_pj_d)
    or a
    sbc hl,de
    ex de,hl
    ld hl,(_pj_sy)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld (_pj_sy),hl

    ld de,5
    add iy,de
    inc ix
    inc ix
    jp vp_loop
vp_done:
    pop iy
    pop ix
    ret

; HL = (A:H:L signed 24 bits) >> 12
shr12:
    call shr11
    sra h
    rr l
    ret

; HL = (A:H:L signed 24 bits) >> 11 (arithmetic): bytes 2, 1 then 3 more bits
shr11:
    ld l,h
    ld h,a
    sra h
    rr l
    sra h
    rr l
    sra h
    rr l
    ret

neg_hl:
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ret

SECTION bss_user

_pj_e:   defs 2
_pj_f:   defs 2
_pj_c:   defs 2
_pj_d:   defs 2
_pj_rec: defs 2
_pj_sx:  defs 2
_pj_sy:  defs 2
_pj_n:   defs 1
ae:      defs 2
af_:     defs 2
se:      defs 1
sf:      defs 1
sx_:     defs 1
sy_:     defs 1
p0:      defs 3
