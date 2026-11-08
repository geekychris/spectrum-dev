; vobj.asm - the view's objects (view.c: the pads, the shadow, the lander,
; its exhaust) into camera space and onto the screen, in Z80N assembly, with
; vproj.asm's camera and perspective:
;
;   void vo_cam(int16_t dx, int16_t dy, int16_t dz, V3s *out)
;       a point (units from the camera) into camera space
;   void vo_scr(const V3s *p, VfVtx *out)
;       a camera-space point (z >= 12) onto the screen (vfill.h's records)
;   void vo_model(const int8_t *v, uint8_t n, const int16_t *k, const V3s *c, V3s *out)
;       n model points (x y z, int8) turned by k (3 x 3, Q12, row-major)
;       and moved to c: out = c + (k v) >> 12
;
; Page 47 at 0x6000. IX kept.

EXTERN vp_smul_us, vp_sh12, vp_scr, vp_dx, vp_yp, _vp_cp, _vp_sp
PUBLIC _vo_cam, _vo_scr, _vo_model, _vo_scr_n

SECTION bss_user
vo_x:   defw 0
vo_y:   defw 0
vo_z:   defw 0
vo_out: defw 0
vo_t:   defw 0, 0
vo_sign: defb 0
vo_v:   defw 0                  ; model points
vo_n:   defb 0
vo_k:   defw 0
vo_c:   defw 0
vo_acc: defb 0, 0, 0            ; a 24-bit sum
vo_in:  defw 0

SECTION PAGE_47

_vo_cam:
    ld hl,2
    add hl,sp
    ld de,vo_x
    ld bc,8
    ldir                        ; vo_x vo_y vo_z vo_out
    push ix
    ; z' = (dz cos - dy sin) >> 12
    ld hl,(vo_z)
    ld de,(_vp_cp)
    call vp_smul_us
    ld (vo_t),hl
    ld (vo_t + 2),de
    ld hl,(vo_y)
    ld de,(_vp_sp)
    call vp_smul_us
    ld bc,(vo_t)
    ld a,c
    sub l
    ld l,a
    ld a,b
    sbc a,h
    ld h,a
    ld bc,(vo_t + 2)
    ld a,c
    sbc a,e
    ld e,a
    ld a,b
    sbc a,d
    ld d,a
    call vp_sh12
    ld ix,(vo_out)
    ld (ix+4),l
    ld (ix+5),h
    ; y' = (dy cos + dz sin) >> 12
    ld hl,(vo_y)
    ld de,(_vp_cp)
    call vp_smul_us
    ld (vo_t),hl
    ld (vo_t + 2),de
    ld hl,(vo_z)
    ld de,(_vp_sp)
    call vp_smul_us
    ld bc,(vo_t)
    add hl,bc
    ex de,hl
    ld bc,(vo_t + 2)
    adc hl,bc
    ex de,hl
    call vp_sh12
    ld (ix+2),l
    ld (ix+3),h
    ld hl,(vo_x)
    ld (ix+0),l
    ld (ix+1),h
    pop ix
    ret

_vo_scr:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; p
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; out
    push ix
    push bc
    pop ix
    ex de,hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vp_dx),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vp_yp),de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; z'
    ld a,h
    or a
    jr nz,vs_z
    ld a,l
    cp 21
    jr nc,vs_z
    ld l,21
vs_z:
    call vp_scr
    pop ix
    ret

;   void vo_scr_n(const V3s *in, VfVtx *out, uint8_t n): n points (vo_scr)
_vo_scr_n:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vo_in),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vo_out),de
    ld a,(hl)
    ld (vo_n),a
    push ix
vsn_pt:
    ld hl,(vo_in)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vp_dx),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vp_yp),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vo_in),hl
    ex de,hl                    ; z'
    ld a,h
    or a
    jr nz,vsn_z
    ld a,l
    cp 21
    jr nc,vsn_z
    ld l,21
vsn_z:
    ld ix,(vo_out)
    call vp_scr
    ld hl,(vo_out)
    ld de,4
    add hl,de
    ld (vo_out),hl
    ld hl,vo_n
    dec (hl)
    jr nz,vsn_pt
    pop ix
    ret

; C:HL = A * DE (A int8, DE int16 with |DE| < 32768), signed 24 bits
m8x16:
    ld b,a
    xor d
    ld (vo_sign),a
    ld a,b
    bit 7,a
    jr z,m_apos
    neg
m_apos:
    ld b,a                      ; |a|
    bit 7,d
    jr z,m_dpos
    xor a
    sub e
    ld e,a
    sbc a,a
    sub d
    ld d,a
m_dpos:
    ld a,d                      ; |k| high
    ld d,b
    mul d,e                     ; |a| * low
    ex de,hl
    ld e,a
    ld d,b
    mul d,e                     ; |a| * high
    ld a,h
    add a,e
    ld h,a
    ld a,d
    adc a,0
    ld c,a                      ; C:HL
    ld a,(vo_sign)
    rla
    ret nc
    xor a                       ; negate C:HL
    sub l
    ld l,a
    ld a,0
    sbc a,h
    ld h,a
    ld a,0
    sbc a,c
    ld c,a
    ret

; one row of k (at HL) times the point (vo_v's x y z): DE = the sum >> 12
krow:
    push hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld hl,(vo_v)
    ld a,(hl)                   ; x
    call m8x16
    ld (vo_acc),hl
    ld a,c
    ld (vo_acc + 2),a
    pop hl
    inc hl
    inc hl
    push hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld hl,(vo_v)
    inc hl
    ld a,(hl)                   ; y
    call m8x16
    call acc_add
    pop hl
    inc hl
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld hl,(vo_v)
    inc hl
    inc hl
    ld a,(hl)                   ; z
    call m8x16
    call acc_add
    ; >> 12: (bits 16..23 : bits 8..15) >> 4
    ld a,(vo_acc + 1)
    ld e,a
    ld a,(vo_acc + 2)
    ld d,a
    ld b,4
    bsra de,b
    ret

acc_add:                        ; vo_acc += C:HL
    ld de,(vo_acc)
    add hl,de
    ld (vo_acc),hl
    ld a,(vo_acc + 2)
    adc a,c
    ld (vo_acc + 2),a
    ret

_vo_model:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vo_v),de
    ld a,(hl)
    ld (vo_n),a
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vo_k),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vo_c),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (vo_out),de
    push ix
vm_pt:
    ld ix,(vo_c)
    ld hl,(vo_k)
    call krow                   ; x
    ld l,(ix+0)
    ld h,(ix+1)
    add hl,de
    push hl
    ld hl,(vo_k)
    ld de,6
    add hl,de
    call krow                   ; y
    ld l,(ix+2)
    ld h,(ix+3)
    add hl,de
    push hl
    ld hl,(vo_k)
    ld de,12
    add hl,de
    call krow                   ; z
    ld l,(ix+4)
    ld h,(ix+5)
    add hl,de
    ld ix,(vo_out)
    ld (ix+4),l
    ld (ix+5),h
    pop hl
    ld (ix+2),l
    ld (ix+3),h
    pop hl
    ld (ix+0),l
    ld (ix+1),h
    ld de,6
    add ix,de
    ld (vo_out),ix
    ld hl,(vo_v)
    inc hl
    inc hl
    inc hl
    ld (vo_v),hl
    ld hl,vo_n
    dec (hl)
    jr nz,vm_pt
    pop ix
    ret
