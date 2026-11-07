; view.asm - the canyon's hot loop in Z80N assembly (render.c)

SECTION code_user

PUBLIC _xform16
EXTERN _cy4, _sy4, _cp4, _sp4

; void xform16(int16_t dx, int16_t dy, int16_t dz, P3 *o)
; the engine's cam_xform with 16-bit deltas (the same values):
;   rx = (dx cy - dz sy) >> 12      rz = (dx sy + dz cy) >> 12
;   y = (dy cp + rz sp) >> 12       z = (rz cp - dy sp) >> 12
; (cy.. = the camera's cos / sin >> 4; products 32 bits)
_xform16:
    push ix
    ld ix,0
    add ix,sp
    ; rx
    ld l,(ix+4)
    ld h,(ix+5)
    ld de,(_cy4)
    call mul_set                ; acc = dx cy
    ld l,(ix+8)
    ld h,(ix+9)
    ld de,(_sy4)
    call mul_sub                ; acc -= dz sy
    call acc_sh12
    ld e,(ix+10)
    ld d,(ix+11)
    ex de,hl
    ld (hl),e
    inc hl
    ld (hl),d                   ; o->x
    ; rz
    ld l,(ix+4)
    ld h,(ix+5)
    ld de,(_sy4)
    call mul_set
    ld l,(ix+8)
    ld h,(ix+9)
    ld de,(_cy4)
    call mul_add
    call acc_sh12
    ld (xf_rz),hl
    ; y
    ld l,(ix+6)
    ld h,(ix+7)
    ld de,(_cp4)
    call mul_set
    ld hl,(xf_rz)
    ld de,(_sp4)
    call mul_add
    call acc_sh12
    ld e,(ix+10)
    ld d,(ix+11)
    ex de,hl
    inc hl
    inc hl
    ld (hl),e
    inc hl
    ld (hl),d                   ; o->y
    ; z
    ld hl,(xf_rz)
    ld de,(_cp4)
    call mul_set
    ld l,(ix+6)
    ld h,(ix+7)
    ld de,(_sp4)
    call mul_sub
    call acc_sh12
    ld e,(ix+10)
    ld d,(ix+11)
    ex de,hl
    inc hl
    inc hl
    inc hl
    inc hl
    ld (hl),e
    inc hl
    ld (hl),d                   ; o->z
    pop ix
    ret

mul_set:                        ; acc = HL * DE
    call smul
    ld (xf_acc),hl
    ld (xf_acc + 2),de
    ret

mul_add:                        ; acc += HL * DE
    call smul
    ld b,d
    ld c,e
    ex de,hl
    ld hl,(xf_acc)
    add hl,de
    ld (xf_acc),hl
    ld hl,(xf_acc + 2)
    adc hl,bc
    ld (xf_acc + 2),hl
    ret

mul_sub:                        ; acc -= HL * DE
    call smul
    ld b,d
    ld c,e
    ex de,hl
    ld hl,(xf_acc)
    or a
    sbc hl,de
    ld (xf_acc),hl
    ld hl,(xf_acc + 2)
    sbc hl,bc
    ld (xf_acc + 2),hl
    ret

acc_sh12:                       ; HL = acc >> 12 (bits 12..27)
    ld hl,(xf_acc + 1)          ; bits 8..23
    ld a,(xf_acc + 3)           ; bits 24..31
    sra a
    rr h
    rr l
    sra a
    rr h
    rr l
    sra a
    rr h
    rr l
    sra a
    rr h
    rr l
    ret

; DEHL = HL * DE, signed 16 x 16 -> 32, by four 8 x 8 MULs
smul:
    ld a,h
    xor d
    ld (xf_sign),a              ; bit 7: the product is negative
    bit 7,h
    jr z,sm_a
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
sm_a:
    bit 7,d
    jr z,sm_b
    xor a
    sub e
    ld e,a
    sbc a,a
    sub d
    ld d,a
sm_b:                           ; |a| in HL, |b| in DE
    ld b,h
    ld c,l
    ld h,d
    ld l,e                      ; BC = a, HL = b
    ld d,b
    ld e,h
    mul d,e                     ; ah bh: bits 16..31
    push de
    ld d,b
    ld e,l
    mul d,e                     ; ah bl
    push de
    ld d,c
    ld e,h
    mul d,e                     ; al bh
    push de
    ld d,c
    ld e,l
    mul d,e                     ; al bl
    ld a,e                      ; A = bits 0..7
    ld l,d
    ld h,0                      ; HL = bits 8.. so far
    pop de
    add hl,de
    ld c,0
    rl c
    pop de
    add hl,de
    jr nc,sm_c
    inc c                       ; C:H:L = bits 8..
sm_c:
    ex de,hl                    ; E = bits 8..15, D = bits 16..23 (partial)
    pop hl                      ; ah bh
    push af
    ld a,l
    add a,d
    ld d,a
    ld a,h
    adc a,c
    ld h,a                      ; H = bits 24..31, D = bits 16..23
    pop af
    ld l,a                      ; L = bits 0..7
    ld a,h
    ld h,e                      ; HL = bits 0..15
    ld e,d
    ld d,a                      ; DE = bits 16..31
    ld a,(xf_sign)
    rla
    ret nc
    xor a                       ; negate DEHL
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

SECTION bss_user
xf_sign: defs 1
xf_acc: defs 4
xf_rz:  defs 2
