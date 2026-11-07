; Tank Duel's camera transform and projection in Z80N assembly (render.c's
; hot path): signed 16 x 16 products from four MULs.
;
;   xf_point   world point (int16 x, y, z at xf_in, camera units) -> camera
;              space (int16 x, y, z at xf_out): the original's cam_xform
;   xf_proj    camera point (x, y, z at xf_in, z >= the near plane) ->
;              screen x, y (int16 at xf_out): cx + x fov / z, cy - y fov / z
;
; Products are truncated towards zero (the C used floor): the picture may
; differ by a fraction of a unit, nothing else depends on it.

SECTION code_user

PUBLIC _xf_point, _xf_proj, _xf_muldiv, _xf_recip
PUBLIC _xf_in, _xf_out, _xf_cam, _xf_cy, _xf_sy, _xf_cp, _xf_sp, _xf_fov, _xf_cx, _xf_cyc

; HL = (HL * DE) >> 14, signed, |HL * DE| < 2^30
smul14:
    ld a,h
    xor d
    ex af,af'                   ; the sign of the product in A' bit 7
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
sm_b:
    ; P = l e + (h e + l d) << 8 + (h d) << 16, into C:B:sm_lo (bits 31..0 as C B . .)
    push de
    push hl
    ld d,l
    mul d,e                     ; l e
    ld a,d                      ; bits 15..8 (bits 7..0 don't reach >> 14)
    ld (sm_t),a
    pop hl
    pop de
    push de
    push hl
    ld d,h
    mul d,e                     ; h e
    ex de,hl                    ; HL = h e
    pop de                      ; D = h, E = l   (old HL)
    pop bc                      ; B = d, C = e   (old DE)
    push de
    ld d,e
    ld e,b
    mul d,e                     ; l d
    add hl,de                   ; HL = h e + l d (17 bits: carry)
    ld a,0
    adc a,a                     ; A = the carry (bit 16 of the middle sum)
    ; P bits 15..8 = sm_t + L (carry into bits 23..16)
    ld e,a                      ; middle carry
    ld a,(sm_t)
    add a,l
    ld (sm_t),a                 ; bits 15..8 of P
    ld a,h
    adc a,0
    ld l,a                      ; bits 23..16 so far
    ld a,e
    adc a,0
    ld h,a                      ; bits 31..24 so far
    pop de                      ; D = h, E = l
    ld e,b
    mul d,e                     ; h d -> bits 31..16
    add hl,de                   ; HL = bits 31..16 of P
    ; >> 14: bits 29..14 = (HL << 2) | (bits 15..14 of P)
    ld a,(sm_t)
    rla
    adc hl,hl
    rla
    adc hl,hl
    ex af,af'
    bit 7,a
    ret z
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ret

; camera: dx = x - cam.x ...; rx = (dx cy - dz sy) >> 14, rz = (dx sy + dz cy) >> 14,
; y = (dy cp + rz sp) >> 14, z = (rz cp - dy sp) >> 14
_xf_point:
    ld hl,(_xf_in)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    push hl
    ld hl,(_xf_cam)
    ex de,hl
    or a
    sbc hl,de
    ld (xf_dx),hl
    pop hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    push hl
    ld hl,(_xf_cam + 2)
    ex de,hl
    or a
    sbc hl,de
    ld (xf_dy),hl
    pop hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld hl,(_xf_cam + 4)
    ex de,hl
    or a
    sbc hl,de
    ld (xf_dz),hl
    ; rx
    ld hl,(xf_dx)
    ld de,(_xf_cy)
    call smul14
    push hl
    ld hl,(xf_dz)
    ld de,(_xf_sy)
    call smul14
    ex de,hl
    pop hl
    or a
    sbc hl,de
    ex de,hl
    ld hl,(_xf_out)
    ld (hl),e
    inc hl
    ld (hl),d
    ; rz
    ld hl,(xf_dx)
    ld de,(_xf_sy)
    call smul14
    push hl
    ld hl,(xf_dz)
    ld de,(_xf_cy)
    call smul14
    pop de
    add hl,de
    ld (xf_rz),hl
    ; y
    ld hl,(xf_dy)
    ld de,(_xf_cp)
    call smul14
    push hl
    ld hl,(xf_rz)
    ld de,(_xf_sp)
    call smul14
    pop de
    add hl,de
    ex de,hl
    ld hl,(_xf_out)
    inc hl
    inc hl
    ld (hl),e
    inc hl
    ld (hl),d
    ; z
    ld hl,(xf_rz)
    ld de,(_xf_cp)
    call smul14
    push hl
    ld hl,(xf_dy)
    ld de,(_xf_sp)
    call smul14
    ex de,hl
    pop hl
    or a
    sbc hl,de
    ex de,hl
    ld hl,(_xf_out)
    ld bc,4
    add hl,bc
    ld (hl),e
    inc hl
    ld (hl),d
    ret

; screen: s = (fov << 14) / z (z >= 64 keeps s < 2^16 for fov <= 255), from
; a table of 64..511 (z above that shifted down: 1/256 precision),
; x' = cx + (x s) >> 14, y' = cy - (y s) >> 14, both clamped to +-16000
_xf_proj:
    ld hl,(_xf_in)
    ld bc,4
    add hl,bc
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = z
    ; s = (fov << 14) / z from the table of 64..511 (render.c fills
    ; xf_recip): z shifted down into that range, s shifted down as much
    ld a,b
    or a
    jr nz,pj_big
    ld a,c
    cp 64
    jr nc,pj_tab                ; < 64 does not happen (the near plane)
    ld c,64
    jr pj_tab
pj_big:
    ld d,0                      ; shifts
pj_shift:
    srl b
    rr c
    inc d
    ld a,b
    cp 2
    jr nc,pj_shift              ; until z < 512
    ld a,d
    ld (xf_k),a
    jr pj_look
pj_tab:
    xor a
    ld (xf_k),a
pj_look:
    ld hl,-64
    add hl,bc
    add hl,hl
    ld de,_xf_recip
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld a,(xf_k)
    or a
    jr z,pj_s
pj_sh:
    srl d
    rr e
    dec a
    jr nz,pj_sh
pj_s:
    ld (xf_s),de
    ; x
    ld hl,(_xf_in)
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    call smul14u                ; HL = (x s) >> 14
    ld de,(_xf_cx)
    add hl,de
    call clamp
    ex de,hl
    ld hl,(_xf_out)
    ld (hl),e
    inc hl
    ld (hl),d
    ; y
    ld hl,(_xf_in)
    inc hl
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    call smul14u
    ex de,hl
    ld hl,(_xf_cyc)
    or a
    sbc hl,de
    call clamp
    ex de,hl
    ld hl,(_xf_out)
    inc hl
    inc hl
    ld (hl),e
    inc hl
    ld (hl),d
    ret

; HL = (HL * xf_s) >> 14 with xf_s unsigned (< 2^16) and |HL| < 2^15:
; the product of |HL| (15 bits) and s, then the sign; saturates at +-32767
smul14u:
    ld a,h
    ex af,af'
    bit 7,h
    jr z,su_a
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
su_a:
    ld de,(xf_s)
    push hl
    push de
    ld d,l
    mul d,e                     ; l e
    ld a,d
    ld (sm_t),a
    pop de
    pop hl
    push hl
    push de
    ld e,d
    ld d,l
    mul d,e                     ; l d
    ex de,hl                    ; HL = l d
    pop de                      ; DE = s
    pop bc                      ; B = h, C = l
    push de
    ld d,b
    mul d,e                     ; h e
    add hl,de
    ld a,0
    adc a,a
    ld e,a
    ld a,(sm_t)
    add a,l
    ld (sm_t),a
    ld a,h
    adc a,0
    ld l,a
    ld a,e
    adc a,0
    ld h,a
    pop de                      ; D = s high
    ld e,b
    mul d,e                     ; h d
    add hl,de                   ; HL = bits 31..16
    ld a,h
    and 0xC0
    jr nz,su_sat                ; >> 14 would not fit 15 bits
    ld a,(sm_t)
    rla
    adc hl,hl
    rla
    adc hl,hl
    bit 7,h
    jr nz,su_sat
    jr su_sign
su_sat:
    ld hl,32767
su_sign:
    ex af,af'
    bit 7,a
    ret z
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ret

; clamp HL to +-16000
clamp:
    bit 7,h
    jr nz,cl_neg
    ld de,16001
    or a
    sbc hl,de
    add hl,de
    ret c
    ld hl,16000
    ret
cl_neg:
    ld de,-16000
    or a
    sbc hl,de
    add hl,de
    ret nc
    ld hl,-16000
    ret

; int16 xf_muldiv(int16 a, int16 b, int16 c): a * b / c, truncated towards
; 0 (the C's (int32)a * b / c), for results that fit 16 bits (the clippers'
; interpolations). A 32 x 16 bit restoring division on |a b|.
_xf_muldiv:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; DE = a, BC = b, HL = c
    ld a,d
    xor b
    xor h
    ex af,af'                   ; the result's sign
    bit 7,h
    jr z,md_c
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
md_c:
    ld (md_div),hl
    ex de,hl
    ld d,b
    ld e,c                      ; HL = a, DE = b
    bit 7,h
    jr z,md_a
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
md_a:
    bit 7,d
    jr z,md_b
    xor a
    sub e
    ld e,a
    sbc a,a
    sub d
    ld d,a
md_b:
    ; P = HL * DE -> md_p (32 bits, little endian)
    push de
    push hl
    ld d,l
    mul d,e                     ; l e
    ld (md_p),de
    pop hl
    pop de
    push de
    push hl
    ld d,h
    mul d,e                     ; h e
    ex de,hl
    pop de                      ; D = h, E = l
    pop bc                      ; B = d, C = e
    push de
    ld d,e
    ld e,b
    mul d,e                     ; l d
    add hl,de
    ld a,0
    adc a,a
    ld e,a                      ; the middle sum's carry
    ld a,(md_p + 1)
    add a,l
    ld (md_p + 1),a
    ld a,h
    adc a,0
    ld l,a
    ld a,e
    adc a,0
    ld h,a
    pop de                      ; D = h
    ld e,b
    mul d,e                     ; h d
    add hl,de
    ld (md_p + 2),hl
    ; Q = P / div: the quotient fits 16 bits (so P's high word < div): 16
    ; steps over the low word, the remainder in HL, the quotient into DE
    ld bc,(md_div)
    ld hl,(md_p + 2)
    ld de,(md_p)
    ld a,16
md_loop:
    sla e
    rl d
    adc hl,hl
    jr c,md_sub                 ; 17 bits: certainly >= div
    or a
    sbc hl,bc
    jr nc,md_one
    add hl,bc
    dec a
    jr nz,md_loop
    jr md_done
md_sub:
    or a
    sbc hl,bc
md_one:
    inc e
    dec a
    jr nz,md_loop
md_done:
    ld (md_p),de
    ld hl,(md_p)
    ex af,af'
    bit 7,a
    ret z
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ret

SECTION bss_user
_xf_in:   defw 0
_xf_out:  defw 0
_xf_cam:  defs 6
_xf_cy:   defw 0
_xf_sy:   defw 0
_xf_cp:   defw 0
_xf_sp:   defw 0
_xf_fov:  defb 0
_xf_cx:   defw 0
_xf_cyc:  defw 0
xf_dx:    defw 0
xf_dy:    defw 0
xf_dz:    defw 0
xf_rz:    defw 0
xf_s:     defw 0
xf_k:     defb 0
_xf_recip: defs 448 * 2
sm_t:     defb 0
md_p:     defs 4
md_div:   defw 0

SECTION code_user

PUBLIC _md_cull, _md_xf, _md_faces, _md_nf, _md_ex, _md_ez, _md_vis, _md_nvis
PUBLIC _md_vin, _md_vout, _md_cd, _md_sd, _md_c

; C:HL = A (int8) * DE (int16), signed, 24 bits
mul8x16:
    ld b,a
    xor d
    ex af,af'                   ; the sign
    ld a,b
    bit 7,a
    jr z,m8_a
    neg
m8_a:
    ld b,a                      ; B = |a|
    bit 7,d
    jr z,m8_b
    xor a
    sub e
    ld e,a
    sbc a,a
    sub d
    ld d,a
m8_b:
    push de
    ld d,b
    mul d,e                     ; |a| * low
    ex de,hl                    ; HL = low product
    pop de
    ld e,b
    mul d,e                     ; |a| * high (D = high byte of |b|)
    ld a,h
    add a,e
    ld h,a
    ld a,d
    adc a,0
    ld c,a                      ; C:HL = |a| |b|
    ex af,af'
    bit 7,a
    ret z
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

; the faces of md_faces (md_nf of 11 bytes) facing an eye at (md_ex, 34,
; md_ez) in the model's frame: nx ex + nz ez > d -> their numbers in md_vis
_md_cull:
    push ix
    ld ix,(_md_faces)
    xor a
    ld (_md_nvis),a
    ld (mc_k),a
mc_loop:
    ld a,(mc_k)
    ld hl,_md_nf
    cp (hl)
    jr nc,mc_done
    ld a,(ix+6)
    ld de,(_md_ex)
    call mul8x16
    ld (mc_acc),hl
    ld a,c
    ld (mc_acc + 2),a
    ld a,(ix+8)
    ld de,(_md_ez)
    call mul8x16                ; C:HL = nz ez
    ld de,(mc_acc)
    add hl,de
    ld a,(mc_acc + 2)
    adc a,c
    ld c,a                      ; C:HL = the dot
    ; minus d (int16, sign-extended): > 0 faces us
    ld e,(ix+9)
    ld d,(ix+10)
    ld a,d
    rla
    sbc a,a                     ; d's sign byte
    ld b,a
    or a
    sbc hl,de
    ld a,c
    sbc a,b                     ; A:HL = dot - d (24 bits)
    bit 7,a
    jr nz,mc_next               ; < 0
    or h
    or l
    jr z,mc_next                ; = 0
    ld hl,_md_nvis
    ld e,(hl)
    inc (hl)
    ld d,0
    ld hl,_md_vis
    add hl,de
    ld a,(mc_k)
    ld (hl),a
mc_next:
    ld de,11
    add ix,de
    ld hl,mc_k
    inc (hl)
    jr mc_loop
mc_done:
    pop ix
    ret

; one vertex (md_vtx at md_vin) -> camera space (v3 at md_vout):
;   X = c.x + ((x cd + z sd) >> 11), R = (z cd - x sd) >> 11,
;   Y = c.y + ypc + ((R sp) >> 14), Z = c.z + R - ysp  (render.c's model())
_md_xf:
    ld hl,(_md_vin)
    ld a,(hl)
    ld (mx_x),a
    inc hl
    ld a,(hl)
    ld (mx_z),a
    ; x cd + z sd
    ld a,(mx_x)
    ld de,(_md_cd)
    call mul8x16
    ld (mc_acc),hl
    ld a,c
    ld (mc_acc + 2),a
    ld a,(mx_z)
    ld de,(_md_sd)
    call mul8x16
    ld de,(mc_acc)
    add hl,de
    ld a,(mc_acc + 2)
    adc a,c
    call sh11                   ; HL = A:HL >> 11
    ld de,(_md_c)
    add hl,de
    ex de,hl
    ld hl,(_md_vout)
    ld (hl),e
    inc hl
    ld (hl),d
    ; R = z cd - x sd
    ld a,(mx_z)
    ld de,(_md_cd)
    call mul8x16
    ld (mc_acc),hl
    ld a,c
    ld (mc_acc + 2),a
    ld a,(mx_x)
    ld de,(_md_sd)
    call mul8x16                ; C:HL = x sd
    ex de,hl
    ld hl,(mc_acc)
    or a
    sbc hl,de
    ld a,(mc_acc + 2)
    sbc a,c
    call sh11
    ld (mx_r),hl
    ; Y = c.y + ypc + (R sp) >> 14
    ld de,(_xf_sp)
    call smul14
    ex de,hl
    ld hl,(_md_vin)
    inc hl
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; ypc
    add hl,de
    ld de,(_md_c + 2)
    add hl,de
    ex de,hl
    ld hl,(_md_vout)
    inc hl
    inc hl
    ld (hl),e
    inc hl
    ld (hl),d
    ; Z = c.z + R - ysp
    ld hl,(_md_vin)
    ld de,4
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; ysp
    ld hl,(mx_r)
    or a
    sbc hl,de
    ld de,(_md_c + 4)
    add hl,de
    ex de,hl
    ld hl,(_md_vout)
    ld bc,4
    add hl,bc
    ld (hl),e
    inc hl
    ld (hl),d
    ret

; HL = A:HL >> 11 (arithmetic, 24-bit A:HL)
sh11:
    ld l,h
    ld h,a                      ; >> 8 (the top byte's sign is in H)
    sra h
    rr l
    sra h
    rr l
    sra h
    rr l
    ret

SECTION bss_user
_md_faces: defw 0
_md_nf:    defb 0
_md_ex:    defw 0
_md_ez:    defw 0
_md_vis:   defs 24
_md_nvis:  defb 0
_md_vin:   defw 0
_md_vout:  defw 0
_md_cd:    defw 0
_md_sd:    defw 0
_md_c:     defs 6
mc_k:      defb 0
mc_acc:    defs 3
mx_x:      defb 0
mx_z:      defb 0
mx_r:      defw 0
