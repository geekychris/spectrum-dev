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
PUBLIC _vo_cam, _vo_scr, _vo_model, _vo_scr_n, _vo_box, _vo_oct

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
vb_lh:  defw 0                  ; vo_box: the box's lo/hi
vb_cam: defw 0
vb_t:   defs 36                 ; the six turned edges: [axis][end] x y z
vb_near: defb 0

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
;   uint8_t vo_box(const int8_t *lohi, const int16_t *k, const V3s *c, VfVtx *out, V3s *cam)
; a box (lo x y z, hi x y z: int8) turned by k (Q12, row-major) and moved
; to c: its 8 corners (bit 0 x, 1 y, 2 z: hi) into camera space (cam) and,
; if none is nearer than 12, onto the screen (out); returns 1 if one is
; (the caller clips)
_vo_box:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vb_lh),de
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
    inc hl
    ld (vo_out),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (vb_cam),de
    push ix
    ; the turned edges: vb_t[a][e] = (k[a] v, k[3+a] v, k[6+a] v) >> 12, v = lohi[3e + a]
    ld ix,vb_t
    ld c,0                      ; a
vb_ax:
    ld b,0                      ; e
vb_end:
    push bc
    ld hl,(vb_lh)
    ld a,b
    add a,a
    add a,b                     ; 3e
    add a,c
    ld e,a
    ld d,0
    add hl,de
    ld a,(hl)                   ; v
    ld (vb_v),a
    ld a,c
    add a,a
    ld e,a
    ld d,0
    ld hl,(vo_k)
    add hl,de                   ; &k[a]
    ld (vb_kp),hl
    call vb_term                ; x
    ld (ix+0),e
    ld (ix+1),d
    ld hl,(vb_kp)
    ld de,6
    add hl,de
    ld (vb_kp),hl
    call vb_term                ; y
    ld (ix+2),e
    ld (ix+3),d
    ld hl,(vb_kp)
    ld de,6
    add hl,de
    ld (vb_kp),hl
    call vb_term                ; z
    ld (ix+4),e
    ld (ix+5),d
    ld de,6
    add ix,de
    pop bc
    inc b
    ld a,b
    cp 2
    jr c,vb_end
    inc c
    ld a,c
    cp 3
    jr c,vb_ax
    ; the corners
    xor a
    ld (vb_near),a
    ld ix,(vb_cam)
    ld c,0                      ; corner
vb_cn:
    push bc
    ; x: c.x + t[0][c & 1].x + t[1][(c >> 1) & 1].x + t[2][c >> 2].x (and y, z)
    ld a,c
    and 1
    call vb_tp                  ; HL = &t[0][bit]
    ld (vb_p0),hl
    ld a,c
    rra
    and 1
    add a,2
    call vb_tp
    ld (vb_p1),hl
    ld a,c
    rra
    rra
    and 1
    add a,4
    call vb_tp
    ld (vb_p2),hl
    ld hl,(vo_c)
    ld (vb_pc),hl
    ld b,3
vb_co:
    push bc
    ld hl,(vb_pc)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vb_pc),hl
    ld hl,(vb_p0)
    call vb_addp
    ld (vb_p0),hl
    ld hl,(vb_p1)
    call vb_addp
    ld (vb_p1),hl
    ld hl,(vb_p2)
    call vb_addp
    ld (vb_p2),hl
    ld (ix+0),e
    ld (ix+1),d
    inc ix
    inc ix
    pop bc
    djnz vb_co
    ; z (just stored) nearer than 12?
    ld a,d
    bit 7,a
    jr nz,vb_nr
    or a
    jr nz,vb_zk
    ld a,e
    cp 12
    jr nc,vb_zk
vb_nr:
    ld a,1
    ld (vb_near),a
vb_zk:
    pop bc
    inc c
    ld a,c
    cp 8
    jr c,vb_cn
    ; onto the screen, if none was near
    ld a,(vb_near)
    or a
    jr nz,vb_done
    ld hl,(vb_cam)
    ld (vo_in),hl
    ld b,8
vb_pr:
    push bc
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
    ex de,hl
    ld a,h
    or a
    jr nz,vb_z21
    ld a,l
    cp 21
    jr nc,vb_z21
    ld l,21
vb_z21:
    ld ix,(vo_out)
    call vp_scr
    ld hl,(vo_out)
    ld de,4
    add hl,de
    ld (vo_out),hl
    pop bc
    djnz vb_pr
vb_done:
    ld a,(vb_near)
    ld l,a
    pop ix
    ret

; DE = (k at vb_kp) * vb_v >> 12
vb_term:
    ld hl,(vb_kp)
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld a,(vb_v)
    call m8x16                  ; C:HL
    ld e,h
    ld d,c
    ld b,4
    bsra de,b
    ret

; A = 2 axis + end: HL = &vb_t[that]
vb_tp:
    ld l,a
    add a,a
    add a,l                     ; x 3
    add a,a                     ; x 6
    ld e,a
    ld d,0
    ld hl,vb_t
    add hl,de
    ret

; DE += (HL) (int16), HL += 2
vb_addp:
    ld a,(hl)
    inc hl
    add a,e
    ld e,a
    ld a,(hl)
    inc hl
    adc a,d
    ld d,a
    ret

SECTION bss_user
vb_v:   defb 0
vb_kp:  defw 0
vb_p0:  defw 0
vb_p1:  defw 0
vb_p2:  defw 0
vb_pc:  defw 0
;   uint8_t vo_oct(const V3s *m, const int16_t *off, VfVtx *out, V3s *cam)
; a level octagon round the camera-space point m: off = { a, b, a sin, a cos,
; b sin, b cos } (a, b its half-sides' offsets r sin 22.5, r cos 22.5, the
; others turned by the camera's pitch); point k = m + (sx[k] , sz[k] sin,
; sz[k] cos) with sx, sz from (a, b, -a, -b): view.c octagon(). Its 8
; points into cam and, unless one is nearer than 12 (returns 1), out
_vo_oct:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vo_c),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vo_k),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (vo_out),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (vb_cam),de
    push ix
    xor a
    ld (vb_near),a
    ld ix,(vb_cam)
    ld hl,oct_pat
    ld (vb_pc),hl
    ld b,8
vo_pt:
    push bc
    ld hl,(vb_pc)
    ld a,(hl)                   ; x: index into off (0 a, 1 b), bit 7 negative
    inc hl
    ld c,(hl)                   ; z: 0 a, 1 b (sin, cos at 2+2i, 3+2i), bit 7 negative
    inc hl
    ld (vb_pc),hl
    ld (vb_v),a
    ; x = m.x +- off[a & 1]
    and 1
    call off_get                ; DE = off[A]
    ld a,(vb_v)
    call neg_if                 ; negated if bit 7 of A
    ld hl,(vo_c)
    call add_m                  ; DE += m.x
    ld (ix+0),e
    ld (ix+1),d
    ; y = m.y +- off[2 + 2 (c & 1)], z = m.z +- off[3 + 2 (c & 1)]
    ld a,c
    and 1
    add a,a
    add a,2
    push af
    call off_get
    ld a,c
    call neg_if
    ld hl,(vo_c)
    inc hl
    inc hl
    call add_m
    ld (ix+2),e
    ld (ix+3),d
    pop af
    inc a
    call off_get
    ld a,c
    call neg_if
    ld hl,(vo_c)
    inc hl
    inc hl
    inc hl
    inc hl
    call add_m
    ld (ix+4),e
    ld (ix+5),d
    ; nearer than 12?
    ld a,d
    bit 7,a
    jr nz,vo_nr
    or a
    jr nz,vo_zk
    ld a,e
    cp 12
    jr nc,vo_zk
vo_nr:
    ld a,1
    ld (vb_near),a
vo_zk:
    ld de,6
    add ix,de
    pop bc
    djnz vo_pt
    ld a,(vb_near)
    or a
    jp nz,vb_done
    ld hl,(vb_cam)
    ld (vo_in),hl
    ld b,8
    jp vb_pr                    ; vo_box's projection loop, then its return

off_get:                        ; DE = off[A]
    add a,a
    ld e,a
    ld d,0
    ld hl,(vo_k)
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ret

neg_if:                         ; DE = -DE if bit 7 of A
    rla
    ret nc
    xor a
    sub e
    ld e,a
    sbc a,a
    sub d
    ld d,a
    ret

add_m:                          ; DE += (HL) (int16)
    ld a,(hl)
    inc hl
    add a,e
    ld e,a
    ld a,(hl)
    adc a,d
    ld d,a
    ret

SECTION rodata_compiler
; the 8 points (view.c octagon()): x (0 a, 1 b, 0x80: negative), z (the same)
oct_pat:
    defb 0, 1,  1, 0,  1, 0x80,  0, 0x81,  0x80, 0x81,  0x81, 0x80,  0x81, 0,  0x80, 1
