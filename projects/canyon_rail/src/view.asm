; view.asm - the canyon's hot loop in Z80N assembly (render.c)

SECTION code_user

PUBLIC _xform16, _edge6, _zscale, _edge6f, _edge_frame
EXTERN _ztab
EXTERN _persp_scale
PUBLIC _cy4, _sy4, _cp4, _sp4

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


; void edge6(const int16_t *dx, int16_t dyb, int16_t dyt, int16_t dz, Edge *e)
; A slab edge's six points (render.c): dx[k] across, at height dyb (points
; 1-4, the floor) or dyt (0 and 5, the rim), all at depth dz (the deltas
; from the camera). xform16's arithmetic, with the products the points
; share made once; then each point in front of the near plane projected:
;   s = persp_scale(z) (at most 32767), px = 128 + (x s >> 12),
;   py = 48 - (y (s/4 + s/8) >> 12)                (render.c's project)
; Edge: c[6] (x, y, z) at 0, px[6] at 36, py[6] at 48, front at 60.
_edge6:
    push ix
    ld ix,0
    add ix,sp
    ; the shared products
    ld l,(ix+10)
    ld h,(ix+11)
    ld de,(_sy4)
    call mul_set
    ld hl,e_A
    call acc_put                ; A = dz sy
    ld l,(ix+10)
    ld h,(ix+11)
    ld de,(_cy4)
    call mul_set
    ld hl,e_B
    call acc_put                ; B = dz cy
    ld l,(ix+6)
    ld h,(ix+7)
    ld de,(_cp4)
    call mul_set
    ld hl,e_Ybc
    call acc_put
    ld l,(ix+6)
    ld h,(ix+7)
    ld de,(_sp4)
    call mul_set
    ld hl,e_Ybs
    call acc_put
    ld l,(ix+8)
    ld h,(ix+9)
    ld de,(_cp4)
    call mul_set
    ld hl,e_Ytc
    call acc_put
    ld l,(ix+8)
    ld h,(ix+9)
    ld de,(_sp4)
    call mul_set
    ld hl,e_Yts
    call acc_put
    ld l,(ix+4)
    ld h,(ix+5)
    ld (e_dx),hl
    ld l,(ix+12)
    ld h,(ix+13)
    ld (e_out),hl
    ld bc,60
    add hl,bc
    ld (hl),1                   ; front, until a point is behind
    xor a
    ld (e_k),a
e6_point:
    ld hl,(e_dx)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (e_dx),hl
    ld (e_d),de
    ; rx = (dx cy - A) >> 12
    ex de,hl
    ld de,(_cy4)
    call mul_set
    ld hl,e_A
    call acc_sub_m
    call acc_sh12
    ld (e_rx),hl
    ; rz = (dx sy + B) >> 12
    ld hl,(e_d)
    ld de,(_sy4)
    call mul_set
    ld hl,e_B
    call acc_add_m
    call acc_sh12
    ld (xf_rz),hl
    ; the rim (points 0 and 5) or the floor
    ld a,(e_k)
    or a
    jr z,e6_top
    cp 5
    jr z,e6_top
    ld hl,e_Ybc
    ld de,e_Ybs
    jr e6_y
e6_top:
    ld hl,e_Ytc
    ld de,e_Yts
e6_y:
    ld (e_ys),de
    ; y = (Yc + rz sp) >> 12
    push hl
    ld hl,(xf_rz)
    ld de,(_sp4)
    call mul_set
    pop hl
    call acc_add_m
    call acc_sh12
    ld (e_ry),hl
    ; z = (rz cp - Ys) >> 12
    ld hl,(xf_rz)
    ld de,(_cp4)
    call mul_set
    ld hl,(e_ys)
    call acc_sub_m
    call acc_sh12
    ld (e_rzz),hl
    call e6_store_project
e6_next:
    ld hl,e_k
    inc (hl)
    ld a,(hl)
    cp 6
    jp nz,e6_point
    pop ix
    ret

; store the point (e_rx, e_ry, e_rzz) as c[e_k] of e_out, and project it
e6_store_project:
    ; store c[k]
    ld de,(e_out)
    ld a,(e_k)
    ld l,a
    add a,a
    add a,l
    add a,a                     ; 6 k
    ld l,a
    ld h,0
    add hl,de
    ld de,(e_rx)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld de,(e_ry)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld de,(e_rzz)
    ld (hl),e
    inc hl
    ld (hl),d
    ; behind the near plane?
    ld hl,(e_rzz)
    bit 7,h
    jr nz,e6_behind
    ld de,12
    or a
    sbc hl,de
    jr c,e6_behind
    ; s = persp_scale(z), at most 32767
    ld hl,(e_rzz)
    call zs_hl
    bit 7,h
    jr z,e6_s_ok
    ld hl,32767
e6_s_ok:
    ld (e_s),hl
    ; px = 128 + (x s >> 12)
    ex de,hl
    ld hl,(e_rx)
    call mul_set
    call acc_sh12
    ld de,128
    add hl,de
    push hl
    ld hl,(e_out)
    ld de,36
    add hl,de
    ld a,(e_k)
    add a,a
    ld e,a
    ld d,0
    add hl,de
    pop de
    ld (hl),e
    inc hl
    ld (hl),d
    ; py = 48 - (y (s/4 + s/8) >> 12)
    ld hl,(e_s)
    srl h
    rr l
    srl h
    rr l                        ; s / 4
    ld d,h
    ld e,l
    srl h
    rr l                        ; s / 8
    add hl,de
    ex de,hl
    ld hl,(e_ry)
    call mul_set
    call acc_sh12
    ex de,hl
    ld hl,48
    or a
    sbc hl,de
    push hl
    ld hl,(e_out)
    ld de,48
    add hl,de
    ld a,(e_k)
    add a,a
    ld e,a
    ld d,0
    add hl,de
    pop de
    ld (hl),e
    inc hl
    ld (hl),d
    ret
e6_behind:
    ld hl,(e_out)
    ld de,60
    add hl,de
    ld (hl),0
    ret

acc_put:                        ; (HL) = acc
    ex de,hl
    ld hl,xf_acc
    ld bc,4
    ldir
    ret

acc_add_m:                      ; acc += (HL)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)
    ld hl,(xf_acc)
    add hl,de
    ld (xf_acc),hl
    ld hl,(xf_acc + 2)
    adc hl,bc
    ld (xf_acc + 2),hl
    ret

acc_sub_m:                      ; acc -= (HL)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)
    ld hl,(xf_acc)
    or a
    sbc hl,de
    ld (xf_acc),hl
    ld hl,(xf_acc + 2)
    sbc hl,bc
    ld (xf_acc + 2),hl
    ret

; uint16_t zscale(uint16_t z) __z88dk_fastcall: persp_scale(z) (fov * 4096 / z),
; from render.c's table ztab[z / 4] for 256 <= z < 4096 (within 1%: the
; picture's points, not the rules'), the division otherwise
_zscale:
zs_hl:
    ld a,h
    or a
    jr z,zs_div                 ; z < 256
    cp 16
    jr nc,zs_div                ; z >= 4096
    srl h
    rr l
    res 0,l                     ; (z / 4) * 2: the entry's offset
    ld de,_ztab
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ret
zs_div:
    push hl
    call _persp_scale
    pop af
    ret


; ---------------------------------------------------------------- edge6f
; void edge_frame(void): the frame's constants for edge6f (after cy4.. are set)
_edge_frame:
    ld hl,30
    ld de,(_cy4)
    call mul_set
    ld hl,k30c
    call acc_put
    ld hl,70
    ld de,(_cy4)
    call mul_set
    ld hl,k70c
    call acc_put
    ld hl,30
    ld de,(_sy4)
    call mul_set
    ld hl,k30s
    call acc_put
    ld hl,70
    ld de,(_sy4)
    call mul_set
    ld hl,k70s
    call acc_put
    ret

; void edge6f(int16_t c, int16_t w, int16_t dyb, int16_t dyt, int16_t dz, Edge *e)
; edge6's points for the picture, quicker: the six points are c - w - 70,
; c - w, c - 30, c + 30, c + w, c + w + 70 across, so their rotated x and
; depth are sums of c's, w's and the frame's 30 / 70 products (exact so
; far); then, the pitch being small and fixed, z = rz - (dy sp >> 12) (the
; cos left out: under a unit) and y = (dy cp + rz sp) >> 12 as before.
; Projection as edge6. Edge layout as edge6.
_edge6f:
    push ix
    ld ix,0
    add ix,sp
    ; XC = c cy - dz sy, ZC = c sy + dz cy, WX = w cy, WZ = w sy
    ld l,(ix+4)
    ld h,(ix+5)
    ld de,(_cy4)
    call mul_set
    ld l,(ix+12)
    ld h,(ix+13)
    ld de,(_sy4)
    call mul_sub
    ld hl,f_XC
    call acc_put
    ld l,(ix+4)
    ld h,(ix+5)
    ld de,(_sy4)
    call mul_set
    ld l,(ix+12)
    ld h,(ix+13)
    ld de,(_cy4)
    call mul_add
    ld hl,f_ZC
    call acc_put
    ld l,(ix+6)
    ld h,(ix+7)
    ld de,(_cy4)
    call mul_set
    ld hl,f_WX
    call acc_put
    ld l,(ix+6)
    ld h,(ix+7)
    ld de,(_sy4)
    call mul_set
    ld hl,f_WZ
    call acc_put
    ; the heights: Yc = dy cp, and dy sp >> 12 for z
    ld l,(ix+8)
    ld h,(ix+9)
    ld de,(_cp4)
    call mul_set
    ld hl,e_Ybc
    call acc_put
    ld l,(ix+8)
    ld h,(ix+9)
    ld de,(_sp4)
    call mul_set
    call acc_sh12
    ld (f_zb),hl
    ld l,(ix+10)
    ld h,(ix+11)
    ld de,(_cp4)
    call mul_set
    ld hl,e_Ytc
    call acc_put
    ld l,(ix+10)
    ld h,(ix+11)
    ld de,(_sp4)
    call mul_set
    call acc_sh12
    ld (f_zt),hl
    ld l,(ix+14)
    ld h,(ix+15)
    ld (e_out),hl
    ld bc,60
    add hl,bc
    ld (hl),1
    xor a
    ld (e_k),a
f_point:
    ; X = XC +- WX +- k (point k), and Z the same
    ld a,(e_k)
    ld hl,f_XC
    call f_sum
    call acc_sh12
    ld (e_rx),hl
    ld a,(e_k)
    or 0x80                     ; (the Z set)
    ld hl,f_ZC
    call f_sum
    call acc_sh12
    ld (xf_rz),hl
    ; the rim (0, 5) or the floor
    ld a,(e_k)
    or a
    jr z,f_top
    cp 5
    jr z,f_top
    ld hl,e_Ybc
    ld de,(f_zb)
    jr f_y
f_top:
    ld hl,e_Ytc
    ld de,(f_zt)
f_y:
    push de
    push hl
    ld hl,(xf_rz)
    ld de,(_sp4)
    call mul_set
    pop hl
    call acc_add_m
    call acc_sh12
    ld (e_ry),hl
    pop de                      ; dy sp >> 12
    ld hl,(xf_rz)
    or a
    sbc hl,de
    ld (e_rzz),hl
    call e6_store_project       ; (edge6's tail: store, project)
    ld hl,e_k
    inc (hl)
    ld a,(hl)
    cp 6
    jr nz,f_point
    pop ix
    ret

; acc = (HL: XC or ZC) + the offsets of point A (bit 7: the Z set): HL's base
; +- W +- 30 / 70 constant
f_sum:
    push af
    ex de,hl
    ld hl,xf_acc
    ex de,hl
    ld bc,4
    ldir                        ; acc = base
    pop af
    ld b,a
    and 0x7F
    ld c,a                      ; C = k
    bit 7,b
    ld hl,f_WX
    jr z,f_s1
    ld hl,f_WZ
f_s1:
    ; points 0, 1: - W; 4, 5: + W
    ld a,c
    cp 2
    jr nc,f_s2
    push bc
    call acc_sub_m
    pop bc
    jr f_s3
f_s2:
    cp 4
    jr c,f_s3
    push bc
    call acc_add_m
    pop bc
f_s3:
    ; 0: - 70, 2: - 30, 3: + 30, 5: + 70
    ld a,c
    bit 7,b
    ld hl,k70c
    ld de,k30c
    jr z,f_s4
    ld hl,k70s
    ld de,k30s
f_s4:
    or a
    jr z,f_sub_hl               ; 0
    cp 5
    jr z,f_add_hl               ; 5
    ex de,hl                    ; the 30s
    cp 2
    jr z,f_sub_hl
    cp 3
    jr z,f_add_hl
    ret                         ; 1, 4
f_sub_hl:
    jp acc_sub_m
f_add_hl:
    jp acc_add_m

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
_cy4:   defs 2                  ; the camera's cos / sin >> 4 (render.c sets them)
_sy4:   defs 2
_cp4:   defs 2
_sp4:   defs 2
xf_sign: defs 1
xf_acc: defs 4
xf_rz:  defs 2
f_XC:   defs 4
f_ZC:   defs 4
f_WX:   defs 4
f_WZ:   defs 4
k30c:   defs 4
k70c:   defs 4
k30s:   defs 4
k70s:   defs 4
f_zb:   defs 2
f_zt:   defs 2
e_A:    defs 4
e_B:    defs 4
e_Ybc:  defs 4
e_Ybs:  defs 4
e_Ytc:  defs 4
e_Yts:  defs 4
e_ys:   defs 2
e_dx:   defs 2
e_out:  defs 2
e_d:    defs 2
e_k:    defs 1
e_rx:   defs 2
e_ry:   defs 2
e_rzz:  defs 2
e_s:    defs 2
