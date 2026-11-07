; vproj.asm - view.c's corner projection for one plane, in Z80N assembly:
;
;   for i in 0..n-1:  sx[i] = c + ((e * y[i] - f * x[i]) >> 11)
;                     sy[i] = d - ((e * x[i] + f * y[i]) >> 12)   (port: half height)
;
; e, f the plane's coefficients (k cos, k sin in Q11), c, d its offsets
; (the screen's middle included), x, y the corners (|x|, |y| < 512). Each
; product is |coefficient| x |corner| in two MULs (the corner's high byte
; is 0 or 1) and kept in 24 bits: view.c only calls this for a plane whose
; products and their sums stay within +-2^23 (|e| + |f| times its largest
; coordinate under 2^22). The shift is arithmetic (floor), as C's on an int32.
;
; void vproj(void): arguments in the variables below (view.c sets them)

SECTION code_user

PUBLIC _vproj
PUBLIC _pj_e, _pj_f, _pj_c, _pj_d, _pj_vx, _pj_vy, _pj_sx, _pj_sy, _pj_n

; A:H:L = |BC| * |DE| (BC < 32768, D = 0 or 1, E the low byte)
umul:
    push de
    ld d,c
    mul d,e                     ; cl * vl
    ex de,hl                    ; HL = cl * vl
    pop de
    push de
    ld d,b
    mul d,e                     ; ch * vl: bytes 1, 2
    ld a,h
    add a,e
    ld h,a
    ld a,d
    adc a,0
    pop de
    bit 0,d
    ret z
    ld e,a                      ; + |BC| << 8
    ld a,h
    add a,c
    ld h,a
    ld a,e
    adc a,b
    ret

; A:H:L = -A:H:L
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

; the product of coefficient k (|k| in kabs, sign in ksgn bit 7) and the
; corner value at (IX + o) (sign in vsgn): A:H:L, signed 24 bits
; in: BC = |k|, DE = |v|, A = the product's sign (bit 7)
smul:
    push af
    call umul
    ld (sm_hi),a
    pop af
    rla
    ld a,(sm_hi)
    ret nc
    jr neg24

_vproj:
    push ix
    ; |e|, |f| and their signs
    ld hl,(_pj_e)
    ld a,h
    ld (se),a
    bit 7,h
    call nz,neg_hl
    ld (ae),hl
    ld hl,(_pj_f)
    ld a,h
    ld (sf),a
    bit 7,h
    call nz,neg_hl
    ld (af_),hl
vp_loop:
    ld a,(_pj_n)
    or a
    jp z,vp_done
    dec a
    ld (_pj_n),a
    ; |x|, sign
    ld hl,(_pj_vx)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (_pj_vx),hl
    ld a,d
    ld (sx_),a
    ex de,hl
    bit 7,h
    call nz,neg_hl
    ld (ax),hl
    ; |y|, sign
    ld hl,(_pj_vy)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (_pj_vy),hl
    ld a,d
    ld (sy_),a
    ex de,hl
    bit 7,h
    call nz,neg_hl
    ld (ay),hl

    ; e y
    ld bc,(ae)
    ld de,(ay)
    ld a,(se)
    ld hl,sy_
    xor (hl)
    call smul
    ld (p0),hl
    ld (p0 + 2),a
    ; f x
    ld bc,(af_)
    ld de,(ax)
    ld a,(sf)
    ld hl,sx_
    xor (hl)
    call smul                   ; A:H:L = f x
    ; e y - f x
    ld e,a
    ld a,(p0)
    sub l
    ld l,a
    ld a,(p0 + 1)
    sbc a,h
    ld h,a
    ld a,(p0 + 2)
    sbc a,e
    call shr11                  ; HL = (A:H:L) >> 11
    ld de,(_pj_c)
    add hl,de
    ex de,hl
    ld hl,(_pj_sx)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld (_pj_sx),hl

    ; e x
    ld bc,(ae)
    ld de,(ax)
    ld a,(se)
    ld hl,sx_
    xor (hl)
    call smul
    ld (p0),hl
    ld (p0 + 2),a
    ; f y
    ld bc,(af_)
    ld de,(ay)
    ld a,(sf)
    ld hl,sy_
    xor (hl)
    call smul
    ; e x + f y
    ld e,a
    ld a,(p0)
    add a,l
    ld l,a
    ld a,(p0 + 1)
    adc a,h
    ld h,a
    ld a,(p0 + 2)
    adc a,e
    call shr12                  ; port: rows at half height (main.c's copper shows each twice)
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
    jp vp_loop
vp_done:
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

_pj_e:  defs 2
_pj_f:  defs 2
_pj_c:  defs 2
_pj_d:  defs 2
_pj_vx: defs 2
_pj_vy: defs 2
_pj_sx: defs 2
_pj_sy: defs 2
_pj_n:  defs 1
ae:     defs 2
af_:    defs 2
se:     defs 1
sf:     defs 1
ax:     defs 2
ay:     defs 2
sx_:    defs 1
sy_:    defs 1
p0:     defs 3
sm_hi:  defs 1
