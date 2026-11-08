; Rolling Steel's physics (the original's phys.c) in Z80N assembly.
;
; port: phys.c line for line, with its 32-bit long arithmetic done exactly
; (rsmath.asm): same contacts, same order, same rounding, so a game plays out
; as the original's does (test.py compares the race hash with
; tools/hostsim.c, which runs the original's C). zsdcc's code for phys.c was
; 38 KB and far too slow for 50 steps a second. Differences in structure, not
; in results:
;   - the ball being stepped is copied to WB and back (direct addresses);
;   - a course's triangles sit in 8K pages, 128 a page with their vertices
;     and bounding sphere (course_c.c builds them), mapped into slot 2;
;   - a collision cell's list of triangles is copied out of its page first;
;   - surf_mu_scale is always FX in phys.c, so mu = fric * surf_fric >> 12.
;
; Code in bank 16 (at 0x0000): banks.c's stubs phys_reset, phys_reset_enemies,
; phys_world_step, phys_marble_step(Ball *, fix, fix) and phys_ground_below
; map it. Its variables, tables and strings stay resident; the small maths
; game.c also calls (isin, icos, isqrt, vlenp) is physmath.asm.

SECTION BANK_16
INCLUDE "rsmac.inc"

PUBLIC _phys_reset_b, _phys_reset_enemies_b, _phys_world_step_b, _phys_marble_step_b
PUBLIC _phys_ground_below_b
PUBLIC _Wd, _phys_sound, _phys_marbles, _phys_nmarbles
PUBLIC _phy_goal, _phy_kill_y, _phy_nacid, _phy_acid, _phy_nzone, _phy_zone
PUBLIC _phy_npillar, _phy_pillar, _phy_nsweep, _phy_sweep, _phy_ncrush, _phy_crush
PUBLIC _phy_nenemy, _phy_enemy, _phy_ncrumble
PUBLIC _phy_gx0, _phy_gz0, _phy_gnx, _phy_gnz, _phy_gpage, _phy_ipage, _phy_tpage
PUBLIC _pc_tri, _pc_sph, _pc_plane, _pc_edge, _pc_segax, _pc_segfull, _pc_contact
PUBLIC _str_crushed, _str_eaten, _str_dissolved, qsin
EXTERN m_sra4, m_sra12, m_sla4, m_sla12, m_sra32, m_sla32, m_neg32, m_umul16, m_smul16, m_mul32s16, m_mul32, m_mul32x
EXTERN m_div32, m_isqrt, m_cmp32
EXTERN p_isin, p_isinl, p_icosl, p_sq, p_vlen, ld32_hl, sext

defc FX = 4096
defc ACCEL = 2785
defc MAXSPEED = 13 * 4096
defc BOUNCE_MIN = 2 * 4096
defc GROUND_NY = 1843
defc SLOT2_HOME = 10            ; what slot 2 holds outside the physics (bank 5's first page)
defc S_BOUNCY = 4
defc SFX_CLACK = 3
defc SFX_THUD = 9

; (BC) = DEHL
st32_bc:
    ld a,l
    ld (bc),a
    inc bc
    ld a,h
    ld (bc),a
    inc bc
    ld a,e
    ld (bc),a
    inc bc
    ld a,d
    ld (bc),a
    ret

; fix dotn(V3 a, n): ((a.x >> 2) nx + (a.y >> 2) ny + (a.z >> 2) nz) >> 10
; a at (HL); n = cn_x, cn_y, cn_z (16-bit). port: 16 x 16 products when the
; halved components fit (exact either way)
p_dotn:
    ld (dn_p),hl
    call q2_at
    jr c,dn_slow
    ld de,(cn_x)
    call m_smul16
    ST32 dn_s
    ld hl,(dn_p)
    ld bc,4
    add hl,bc
    call q2_at
    jr c,dn_slow
    ld de,(cn_y)
    call m_smul16
    ADD32 dn_s
    ST32 dn_s
    ld hl,(dn_p)
    ld bc,8
    add hl,bc
    call q2_at
    jr c,dn_slow
    ld de,(cn_z)
    call m_smul16
    ADD32 dn_s
    SRA32 10
    ret
dn_slow:
    ld hl,(dn_p)
    push hl
    call ld32_hl
    SRA2
    ld bc,(cn_x)
    call m_mul32s16
    ST32 dn_s
    pop hl
    push hl
    ld bc,4
    add hl,bc
    call ld32_hl
    SRA2
    ld bc,(cn_y)
    call m_mul32s16
    ADD32 dn_s
    ST32 dn_s
    pop hl
    ld bc,8
    add hl,bc
    call ld32_hl
    SRA2
    ld bc,(cn_z)
    call m_mul32s16
    ADD32 dn_s
    SRA32 10
    ret

; ======================================================================
; contact(b = WB, n = cn_*, pen = c_pen, cls = c_cls, vs = c_vs)
; ======================================================================
p_contact:
    ld hl,(_pc_contact)
    inc hl
    ld (_pc_contact),hl
    LD32 c_pen                  ; port: the push moves each axis by at most |pen| + 1
    call odo_add
    ; b->p += n * pen >> 12
    LD32 c_pen
    ld bc,(cn_x)
    call m_mul32s16
    SRA12
    ADD32 WB + B_PX
    ST32 WB + B_PX
    LD32 c_pen
    ld bc,(cn_y)
    call m_mul32s16
    SRA12
    ADD32 WB + B_PY
    ST32 WB + B_PY
    LD32 c_pen
    ld bc,(cn_z)
    call m_mul32s16
    SRA12
    ADD32 WB + B_PZ
    ST32 WB + B_PZ
    ; if (ny > GROUND_NY) contact_ground = 1
    ld hl,GROUND_NY
    ld de,(cn_y)
    or a
    sbc hl,de                   ; GROUND_NY - ny < 0 ?
    jp pe,ct_gov
    jp p,ct_ng
    jr ct_g
ct_gov:
    jp m,ct_ng
ct_g:
    ld a,1
    ld (contact_ground),a
ct_ng:
    ; rel = b->v - vs; vn = dotn(rel, n)
    LD32 WB + B_VX
    SUB32 c_vs
    ST32 c_rel
    LD32 WB + B_VY
    SUB32 c_vs + 4
    ST32 c_rel + 4
    LD32 WB + B_VZ
    SUB32 c_vs + 8
    ST32 c_rel + 8
    ld hl,c_rel
    call p_dotn
    ; if (vn >= 0) return
    bit 7,d
    ret z
    call m_neg32
    ST32 c_mvn                  ; -vn
    ; e = max(surf_bounce[cls], b->bounce); if (-vn < BOUNCE_MIN) e = 0
    ld a,(c_cls)
    ld l,a
    ld h,0
    add hl,hl
    ld bc,surf_bounce
    add hl,bc
    ld e,(hl)
    inc hl
    ld d,(hl)
    ex de,hl
    ld de,0                     ; surf_bounce[cls] as a long
    ST32 c_e
    LD32 WB + B_BOUNCE
    CMP32 c_e                   ; bounce < surf ?
    jr c,ct_e1
    jr z,ct_e1
    ST32 c_e
ct_e1:
    LD32 c_mvn
    ld bc,BOUNCE_MIN
    ld a,d
    or e
    jr nz,ct_e2                 ; -vn >= 65536
    or a
    sbc hl,bc
    jr nc,ct_e2
    CLR32 c_e
ct_e2:
    ; jn = -vn + (-vn * e >> 12)
    LD32 c_mvn
    MUL32 c_e
    SRA12
    ADD32 c_mvn
    ST32 c_jn
    ; b->v += n * jn >> 12
    LD32 c_jn
    ld bc,(cn_x)
    call m_mul32s16
    SRA12
    ADD32 WB + B_VX
    ST32 WB + B_VX
    LD32 c_jn
    ld bc,(cn_y)
    call m_mul32s16
    SRA12
    ADD32 WB + B_VY
    ST32 WB + B_VY
    LD32 c_jn
    ld bc,(cn_z)
    call m_mul32s16
    SRA12
    ADD32 WB + B_VZ
    ST32 WB + B_VZ
    ; r = -(n * b->r >> 12)  (each fits in 16 bits)
    ld hl,(cn_x)
    call r_of
    ld (c_rx),hl
    ld hl,(cn_y)
    call r_of
    ld (c_ry),hl
    ld hl,(cn_z)
    call r_of
    ld (c_rz),hl
    ; vc.x = b->v.x - vs.x + ((b->w.y * rz - b->w.z * ry) >> 12)
    LD32 WB + B_WY
    ld bc,(c_rz)
    call m_mul32s16
    ST32 c_t
    LD32 WB + B_WZ
    ld bc,(c_ry)
    call m_mul32s16
    call sub_from_t             ; DEHL = c_t - DEHL
    SRA12
    ADD32 WB + B_VX
    SUB32 c_vs
    ST32 c_vc
    ; vc.y = b->v.y - vs.y + ((b->w.z * rx - b->w.x * rz) >> 12)
    LD32 WB + B_WZ
    ld bc,(c_rx)
    call m_mul32s16
    ST32 c_t
    LD32 WB + B_WX
    ld bc,(c_rz)
    call m_mul32s16
    call sub_from_t
    SRA12
    ADD32 WB + B_VY
    SUB32 c_vs + 4
    ST32 c_vc + 4
    ; vc.z = b->v.z - vs.z + ((b->w.x * ry - b->w.y * rx) >> 12)
    LD32 WB + B_WX
    ld bc,(c_ry)
    call m_mul32s16
    ST32 c_t
    LD32 WB + B_WY
    ld bc,(c_rx)
    call m_mul32s16
    call sub_from_t
    SRA12
    ADD32 WB + B_VZ
    SUB32 c_vs + 8
    ST32 c_vc + 8
    ; vn = dotn(vc, n); vt = vc - n * vn >> 12; J = -(vt * 1170 >> 12)
    ld hl,c_vc
    call p_dotn
    ST32 c_vn
    ld ix,c_vc
    ld iy,c_j
    ld hl,(cn_x)
    call ct_jcomp
    ld hl,(cn_y)
    call ct_jcomp
    ld hl,(cn_z)
    call ct_jcomp
    ; mu = b->fric * surf_fric[cls] >> 12; mj = mu * jn >> 12
    ld a,(c_cls)
    ld l,a
    ld h,0
    add hl,hl
    ld bc,surf_fric
    add hl,bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    LD32 WB + B_FRIC
    call m_mul32s16
    SRA12
    MUL32 c_jn
    SRA12
    ST32 c_mj
    ; jl = vlen(J); if (jl > mj && jl > 0) J = J * (mj >> 4) / (jl >> 4 ? jl >> 4 : 1)
    ld hl,c_j
    call p_vlen
    ST32 c_jl
    CMP32 c_mj                  ; jl <= mj: no clamp
    jp c,ct_noclamp
    jp z,ct_noclamp
    ld a,d                      ; jl > 0
    bit 7,a
    jp nz,ct_noclamp
    or e
    or h
    or l
    jp z,ct_noclamp
    SRA4
    ld a,d
    or e
    or h
    or l
    jr nz,ct_jl4
    ld l,1
ct_jl4:
    ST32 c_jl4
    LD32 c_mj
    SRA4
    ST32 c_mj4
    ld hl,c_j
    call ct_clamp
    ld hl,c_j + 4
    call ct_clamp
    ld hl,c_j + 8
    call ct_clamp
ct_noclamp:
    ; b->v += J
    LD32 WB + B_VX
    ADD32 c_j
    ST32 WB + B_VX
    LD32 WB + B_VY
    ADD32 c_j + 4
    ST32 WB + B_VY
    LD32 WB + B_VZ
    ADD32 c_j + 8
    ST32 WB + B_VZ
    ; spin: c = n x J >> 12; b->w -= c * rot_k >> 12
    ; cx = (ny * J.z - nz * J.y) >> 12
    LD32 c_j + 8
    ld bc,(cn_y)
    call m_mul32s16
    ST32 c_t
    LD32 c_j + 4
    ld bc,(cn_z)
    call m_mul32s16
    call sub_from_t
    SRA12
    ld bc,WB + B_WX
    call ct_spin
    ; cy = (nz * J.x - nx * J.z) >> 12
    LD32 c_j
    ld bc,(cn_z)
    call m_mul32s16
    ST32 c_t
    LD32 c_j + 8
    ld bc,(cn_x)
    call m_mul32s16
    call sub_from_t
    SRA12
    ld bc,WB + B_WY
    call ct_spin
    ; cz = (nx * J.y - ny * J.x) >> 12
    LD32 c_j + 4
    ld bc,(cn_x)
    call m_mul32s16
    ST32 c_t
    LD32 c_j
    ld bc,(cn_y)
    call m_mul32s16
    call sub_from_t
    SRA12
    ld bc,WB + B_WZ
    jp ct_spin

; HL = -((HL * b->r) >> 12) for a normal component HL
r_of:
    ex de,hl
    ld hl,(WB + B_R)            ; r fits in 16 bits (0.5 - 0.7 units)
    call m_smul16
    SRA12
    call m_neg32
    ret

; DEHL = c_t - DEHL
sub_from_t:
    call m_neg32
    ADD32 c_t
    ret

; one component: vt = vc - (n * vn >> 12); J = -(vt * 1170 >> 12)
; n component in HL; vc at IX, J at IY (both advanced by 4)
ct_jcomp:
    ld b,h
    ld c,l
    LD32 c_vn
    call m_mul32s16
    SRA12
    call m_neg32
    ld c,(ix + 0)
    ld b,(ix + 1)
    add hl,bc
    ex de,hl
    ld c,(ix + 2)
    ld b,(ix + 3)
    adc hl,bc
    ex de,hl                    ; vt
    MULC 1170
    SRA12
    call m_neg32
    ld (iy + 0),l
    ld (iy + 1),h
    ld (iy + 2),e
    ld (iy + 3),d
    ld bc,4
    add ix,bc
    add iy,bc
    ret

; (HL) = (HL) * mj4 / jl4
ct_clamp:
    push hl
    call ld32_hl
    MUL32 c_mj4
    DIV32 c_jl4
    pop bc
    ld a,l
    ld (bc),a
    inc bc
    ld a,h
    ld (bc),a
    inc bc
    ld a,e
    ld (bc),a
    inc bc
    ld a,d
    ld (bc),a
    ret

; (BC) -= DEHL * rot_k >> 12
ct_spin:
    push bc
    MUL32 WB + B_ROTK
    SRA12
    call m_neg32
    pop bc
    ld a,(bc)
    add a,l
    ld (bc),a
    inc bc
    ld a,(bc)
    adc a,h
    ld (bc),a
    inc bc
    ld a,(bc)
    adc a,e
    ld (bc),a
    inc bc
    ld a,(bc)
    adc a,d
    ld (bc),a
    ret

; ======================================================================
; the ball against the course's triangles
; ======================================================================

;  IY = the triangle HL (its page from the first << 8 | its slot), mapped in slot 2
map_tri:
    ld a,(_phy_tpage)
    add a,h
    nextreg 0x52,a
    ld e,l
    ld d,TRI_SIZE
    mul d,e
    ld hl,TRI_WINDOW
    add hl,de
    push hl
    pop iy
    ret

; DEHL = the 24-bit value at IY + A, sign-extended
ldy24:
    push iy
    pop hl
    add hl,a
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    ex de,hl
    ld e,a
    rla
    sbc a,a
    ld d,a
    ret

; d2 accumulate helper: DEHL = DEHL >> 4 then compare |.| with ht_rr, square to sum
; hit_tri(b = WB, triangle id HL)
p_hit_tri:
    push hl
    ld hl,(_pc_tri)
    inc hl
    ld (_pc_tri),hl
    pop hl
    call map_tri
    ; group, cls
    ld a,(iy + T_CLS)
    ld (c_cls),a
    ld a,(iy + T_GROUP)
    ld (ht_group),a
    or a
    jr z,ht_g0
    call crumble_state          ; if (group && Wd.crumble[group - 1].state == 2) return
    cp 2
    ret z
ht_g0:
    ; the bounding sphere: d = (b->p - sp) >> 4, rr = (sp.r + b->r) >> 4.
    ; port: in 16 bits where that is exact: both radii are below 65536 (a
    ; ball's is 0.5-0.7 units), so rr < 8192, and an axis with |b->p - sp|
    ; >= 2^17 has |dx| >= 8192 > rr: rejected without the rest
    ld l,(iy + T_SPHR)
    ld h,(iy + T_SPHR + 1)
    ld bc,(WB + B_R)
    add hl,bc
    rr h
    rr l
    srl h
    rr l
    srl h
    rr l
    srl h
    rr l
    ld (ht_rr16),hl
    ld a,T_SPH
    ld de,WB + B_PX
    call ht_ax16
    ret c
    ld (ht_ax),hl
    ld a,T_SPH + 3
    ld de,WB + B_PY
    call ht_ax16
    ret c
    ld (ht_ay),hl
    ld a,T_SPH + 6
    ld de,WB + B_PZ
    call ht_ax16
    ret c
    ld (ht_az),hl
    ; if (dx*dx + dy*dy + dz*dz > rr*rr) return (|d| <= rr < 8192: no overflow)
    ld d,h
    ld e,l
    call m_umul16
    ld (ht_s),hl
    ld (ht_s + 2),de
    ld hl,(ht_ay)
    ld d,h
    ld e,l
    call m_umul16
    ld bc,(ht_s)
    add hl,bc
    ld (ht_s),hl
    ex de,hl
    ld bc,(ht_s + 2)
    adc hl,bc
    ld (ht_s + 2),hl
    ld hl,(ht_ax)
    ld d,h
    ld e,l
    call m_umul16
    ld bc,(ht_s)
    add hl,bc
    ld (ht_s),hl
    ex de,hl
    ld bc,(ht_s + 2)
    adc hl,bc
    ld (ht_s + 2),hl
    ld hl,(ht_rr16)
    ld d,h
    ld e,l
    call m_umul16               ; rr * rr
    ld bc,(ht_s)
    or a
    sbc hl,bc
    ex de,hl
    ld bc,(ht_s + 2)
    sbc hl,bc
    jp c,ht_l2out               ; rr * rr < the sum
    ; d = b->p - a; dist = dotn(d, n)
    ld hl,(_pc_sph)
    inc hl
    ld (_pc_sph),hl
    ld l,(iy + T_N)
    ld h,(iy + T_N + 1)
    ld (cn_x),hl
    ld l,(iy + T_N + 2)
    ld h,(iy + T_N + 3)
    ld (cn_y),hl
    ld l,(iy + T_N + 4)
    ld h,(iy + T_N + 5)
    ld (cn_z),hl
    ; port: (d >> 2) in 16 bits when |d| < 2^17 on each axis (else phys.c's
    ; 32-bit dotn, below); the products and their sum are exact either way
    ld a,T_A
    ld de,WB + B_PX
    call ht_d24
    call ht_q2
    jr c,ht_dslow
    ld de,(cn_x)
    call m_smul16
    ld (ht_s),hl
    ld (ht_s + 2),de
    ld a,T_A + 3
    ld de,WB + B_PY
    call ht_d24
    call ht_q2
    jr c,ht_dslow
    ld de,(cn_y)
    call m_smul16
    ld bc,(ht_s)
    add hl,bc
    ld (ht_s),hl
    ex de,hl
    ld bc,(ht_s + 2)
    adc hl,bc
    ld (ht_s + 2),hl
    ld a,T_A + 6
    ld de,WB + B_PZ
    call ht_d24
    call ht_q2
    jr c,ht_dslow
    ld de,(cn_z)
    call m_smul16
    ADD32 ht_s
    SRA32 10
    jr ht_dset
ht_dslow:
    ld a,T_A
    call ldy24
    call neg_add_px
    ST32 ht_d
    ld a,T_A + 3
    call ldy24
    call m_neg32
    ADD32 WB + B_PY
    ST32 ht_d + 4
    ld a,T_A + 6
    call ldy24
    call m_neg32
    ADD32 WB + B_PZ
    ST32 ht_d + 8
    ld hl,ht_d
    call p_dotn
ht_dset:
    ST32 ht_dist
    ; if (dist > b->r || dist < -(b->r >> 1)) return
    CMP32 WB + B_R
    jr c,ht_d1
    jr z,ht_d1
    SUB32 WB + B_R              ; dist - r > 0
    jp pl_out
ht_d1:
    LD32 WB + B_R
    SRA32 1
    call m_neg32
    CMP32 ht_dist               ; -(r >> 1) > dist ?
    jr c,ht_d2
    jr z,ht_d2
    SUB32 ht_dist               ; -(r >> 1) - dist > 0
    jp pl_out
ht_d2:
    ld hl,(_pc_plane)
    inc hl
    ld (_pc_plane),hl
    ; the plane it faces most: |n| components
    ld hl,(cn_x)
    call abs16
    ld (ht_anx),hl
    ld hl,(cn_y)
    call abs16
    ld (ht_any),hl
    ld hl,(cn_z)
    call abs16
    ld (ht_anz),hl
    ; if (any >= anx && any >= anz) -> u = x, v = z
    ld de,(ht_anx)
    ld hl,(ht_any)
    or a
    sbc hl,de
    jr c,ht_p2
    ld de,(ht_anz)
    ld hl,(ht_any)
    or a
    sbc hl,de
    jr c,ht_p2
    ld a,0                      ; u offset x
    ld c,8                      ; v offset z
    jr ht_pset
ht_p2:
    ld de,(ht_anz)              ; else if (anx >= anz) -> u = y, v = z
    ld hl,(ht_anx)
    or a
    sbc hl,de
    jr c,ht_p3
    ld a,4
    ld c,8
    jr ht_pset
ht_p3:
    ld a,0                      ; else u = x, v = y
    ld c,4
ht_pset:
    ld (ht_uo),a
    ld a,c
    ld (ht_vo),a
    ld a,(ht_uo)
    ld c,a
    xor a
    call add_voff
    ld (ht_uov),a
    ld a,(ht_vo)
    ld c,a
    xor a
    call add_voff
    ld (ht_vov),a
    ; port: w = p - vertex in (u, v) for a, b, c (exact 32-bit); then an edge
    ; from F to T has eu = wF.u - wT.u, fu = wF.u (and v), as phys.c's
    ; bu - au and pu - au
    ld hl,ht_w
    ld (fs_ptr),hl
    ld a,T_A
    call w_vert
    ld a,T_B
    call w_vert
    ld a,T_C
    call w_vert
    ; s1 = cross2(bu - au, bv - av, pu - au, pv - av)
    xor a
    ld (ce_k),a
    ld ix,ht_w
    ld hl,ht_w + 8
    call cross2w
    ld (ht_s1),a
    ld a,1
    ld (ce_k),a
    ld ix,ht_w + 8
    ld hl,ht_w + 16
    call cross2w
    ld (ht_s2),a
    ld a,2
    ld (ce_k),a
    ld ix,ht_w + 16
    ld hl,ht_w
    call cross2w
    ; (s1 >= 0 && s2 >= 0 && s3 >= 0) || (s1 <= 0 && s2 <= 0 && s3 <= 0)
    ; signs: bit 0 = > 0, bit 1 = < 0 (0 = zero)
    ld b,a
    ld a,(ht_s1)
    or b
    ld b,a
    ld a,(ht_s2)
    or b
    cp 3
    jr z,ht_edges
    ; inside: contact(b, n, b->r - dist, cls, still)
    LD32 WB + B_R
    SUB32 ht_dist
    ST32 c_pen
    call clr_vs
    call p_contact
    jp ht_touch
ht_edges:
    ld hl,(_pc_edge)
    inc hl
    ld (_pc_edge),hl
    ; the nearest point on the three edges. sg_m = r + 32, sg_mm = -sg_m for
    ; ht_seg's skip: the nearest point q of a segment lies in its box, and if
    ; |p - q| >= r + 16 along one axis, ((p - q) >> 4)^2 alone is > (r >> 4)^2
    ; = r2, so that segment's d2 can't be the one under r2
    LD32 WB + B_R
    ld bc,32
    add hl,bc
    jr nc,he_m1
    inc de
he_m1:
    ST32 sg_m
    call m_neg32
    ST32 sg_mm
    ld hl,0x7FFF
    ld (ht_bd + 2),hl
    ld hl,0xFFFF
    ld (ht_bd),hl
    ; port: segments that are far from the ball (edge_far) first: none
    ; left, no contact (bd stays above r2)
    xor a
    call edge_far
    ld (he_far),a
    ld a,1
    call edge_far
    ld (he_far + 1),a
    ld a,2
    call edge_far
    ld (he_far + 2),a
    ld hl,he_far
    ld a,(hl)
    inc hl
    or (hl)
    inc hl
    or (hl)
    ret z                       ; all three far: 0 = far (edge_far's Z)
    ; port: p - a, p - b, p - c once (fs_pv), then each segment from them in
    ; 16-bit products where those are exact (fs_seg; else ht_seg)
    ld hl,fs_pv
    ld (fs_ptr),hl
    ld a,T_A
    call fs_vert
    ld a,T_B
    call fs_vert
    ld a,T_C
    call fs_vert
    ld a,(he_far)
    or a
    jr z,he_s1
    ld hl,fs_pv
    ld de,fs_pv + 12
    ld a,T_A
    ld c,T_B
    call fs_seg
he_s1:
    ld a,(he_far + 1)
    or a
    jr z,he_s2
    ld hl,fs_pv + 12
    ld de,fs_pv + 24
    ld a,T_B
    ld c,T_C
    call fs_seg
he_s2:
    ld a,(he_far + 2)
    or a
    jr z,he_s3
    ld hl,fs_pv + 24
    ld de,fs_pv
    ld a,T_C
    ld c,T_A
    call fs_seg
he_s3:
    ; r2 = (b->r >> 4)^2; if (bd >= r2 || bd <= 0) return
    LD32 WB + B_R
    SRA4
    call p_sq
    ST32 ht_r2
    LD32 ht_bd
    CMP32 ht_r2
    ret nc
    bit 7,d
    ret nz
    ld a,d
    or e
    or h
    or l
    ret z
    ; len = isqrt(bd) << 4; e = (b->p - best) << 12 / len
    call m_isqrt
    SLA4
    ST32 ht_len
    LD32 WB + B_PX
    SUB32 ht_best
    SLA12
    DIV32 ht_len
    ld (cn_x),hl
    LD32 WB + B_PY
    SUB32 ht_best + 4
    SLA12
    DIV32 ht_len
    ld (cn_y),hl
    LD32 WB + B_PZ
    SUB32 ht_best + 8
    SLA12
    DIV32 ht_len
    ld (cn_z),hl
    LD32 WB + B_R
    SUB32 ht_len
    ST32 c_pen
    call clr_vs
    call p_contact
ht_touch:
    ; if (group && Wd.crumble[group - 1].state == 0) tile_touched = group
    ld a,(ht_group)
    or a
    ret z
    call crumble_state
    or a
    ret nz
    ld a,(ht_group)
    ld (tile_touched),a
    ret

; port: the sum-of-squares test failed: note (floor(L1 * 147 / 256) - rr) / 2
; for hc_marble, a lower bound of half the distance (in dx units) beyond rr
; (L2 >= L1 / sqrt 3 > L1 * 147 / 256); halved because the distance changes
; by up to sqrt 3 times an axis
ht_l2out:
    ld hl,(mc_rec)
    ld a,h
    or l
    ret z
    ld hl,(ht_ax)
    ld de,(ht_ay)
    add hl,de
    ld de,(ht_az)
    add hl,de                   ; L1 < 3 * 8192
    ld de,147
    call m_umul16
    ld l,h                      ; >> 8
    ld h,e
    ld de,(ht_rr16)
    or a
    sbc hl,de
    jr c,l2o_none
    jr z,l2o_none
    srl h
    rr l
    jr rec_slack
l2o_none:
    ret

; port: the plane test failed by DEHL (> 0, Q12): note DEHL / 32 for
; hc_marble (dist changes by at most 1.73 times an axis' change + 8)
pl_out:
    ld a,(mc_rec)
    ld c,a
    ld a,(mc_rec + 1)
    or c
    ret z
    ld b,5
    call m_sra32
    ld a,d
    or e
    jr nz,rec_max
    jr rec_slack

; slack HL (16 bits) -> (mc_rec) (at most 255) and the stamp
rec_max:
    ld hl,255
rec_slack:
    ld a,h
    or a
    ld a,l
    jr z,rs_1
    ld a,255
rs_1:
    or a
    ret z                       ; 0: nothing worth keeping
    ld hl,(mc_rec)
    ld (hl),a
    ld hl,(cs_stp)
    ld de,(odo4)
    ld (hl),e
    inc hl
    ld (hl),d
    ret

; HL:DE (high, low) = the long at DE (a ball coordinate) - the 24-bit value
; at IY + A (a triangle's)
ht_d24:
    push iy
    pop hl
    add hl,a
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld a,(hl)
    ld (ax_lo),bc
    ld c,a
    rla
    sbc a,a
    ld b,a                      ; BC = its high word
    ex de,hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    push hl
    ex de,hl                    ; HL = the low word
    ld de,(ax_lo)
    or a
    sbc hl,de
    ex (sp),hl                  ; (sp) = the low difference
    ld e,(hl)
    inc hl
    ld d,(hl)
    ex de,hl
    sbc hl,bc                   ; the high difference
    pop de
    ret

; C if the long HL:DE is outside -2^17 .. 2^17 - 1
ht_in17:
    ld a,h
    or a
    jr z,hi_pos
    inc a
    jr nz,hi_out
    ld a,l
    cp 0xFE
    ret                         ; C if below 0xFFFE
hi_pos:
    ld a,l
    cp 2
    ccf                         ; C if 2 or more
    ret
hi_out:
    scf
    ret

; the sphere test on an axis: HL = |d >> 4| for d = (DE) - (IY + A); C if
; outside +-rr (then HL is junk; the slack |dx| - rr is noted for hc_marble)
ht_ax16:
    call ht_d24
    call ht_in17
    jr nc,hax16_in
    ld de,8192                  ; |d| >= 2^17: |dx| >= 8192
    ld hl,(ht_rr16)
    jr hax16_rec
hax16_in:
    ld b,4
    bsrl de,b                   ; d >> 4 in 16 bits: bits 4-19
    ld a,l
    and 15
    swapnib
    or d
    ld d,a
    bit 7,d
    jr z,hax16_p
    xor a
    sub e
    ld e,a
    sbc a,a
    sub d
    ld d,a
hax16_p:
    ld hl,(ht_rr16)
    or a
    sbc hl,de                   ; C if rr < |dx|
    ex de,hl
    ret nc
    ex de,hl                    ; DE = |dx|, HL = rr - |dx|
    ld hl,(ht_rr16)
hax16_rec:
    ; slack = |dx| - rr (DE - HL), at most 255, and the odometer, if asked
    ex de,hl
    or a
    sbc hl,de                   ; slack (>= 1)
    ld a,h
    or a
    ld a,l
    jr z,hax16_s
    ld a,255
hax16_s:
    ld hl,(mc_rec)
    ld e,a
    ld a,h
    or l
    jr z,hax16_c
    ld (hl),e
    ld hl,(cs_stp)
    ld de,(odo4)
    ld (hl),e
    inc hl
    ld (hl),d
hax16_c:
    scf
    ret

; HL = HL:DE >> 2 in 16 bits; C if |HL:DE| >= 2^17 (then not)
ht_q2:
    call ht_in17
    ret c
    ld b,2
    bsrl de,b
    ld a,l
    and 3
    rrca
    rrca
    or d
    ld d,a
    ex de,hl
    or a
    ret

; A = Wd.crumble[ht_group - 1].state (low byte)
crumble_state:
    ld a,(ht_group)
    dec a
    ld l,a
    ld h,0
    ld d,h
    ld e,l
    add hl,hl
    add hl,hl
    add hl,de
    add hl,hl                   ; * 10
    ld de,_Wd + W_CRUMBLE + CR_STATE
    add hl,de
    ld a,(hl)
    ret

; DEHL = WB.p.x - DEHL
neg_add_px:
    call m_neg32
    ADD32 WB + B_PX
    ret

; sphere test, x: DEHL = sp.x; returns DEHL = (b->p.x - sp.x) >> 4, C if outside +-rr
ht_axis:
    ld bc,WB + B_PX
ht_axis2:
    push bc
    call m_neg32
    pop bc
    ld a,(bc)
    add a,l
    ld l,a
    inc bc
    ld a,(bc)
    adc a,h
    ld h,a
    inc bc
    ld a,(bc)
    adc a,e
    ld e,a
    inc bc
    ld a,(bc)
    adc a,d
    ld d,a
    SRA4
    ; dx > rr or dx < -rr: outside
    push hl
    push de
    CMP32 ht_rr                 ; dx < rr ... dx > rr if not (C or Z)
    jr c,hax_1
    jr z,hax_1
    pop de
    pop hl
    scf
    ret
hax_1:
    pop de
    pop hl
    push hl
    push de
    call m_neg32                ; -dx > rr ?
    CMP32 ht_rr
    jr c,hax_2
    jr z,hax_2
    pop de
    pop hl
    scf
    ret
hax_2:
    pop de
    pop hl
    or a
    ret

; HL = |HL|
abs16:
    bit 7,h
    ret z
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ret

; w (u, v) = p - (the vertex at IY + A), 2 longs into (fs_ptr), which moves on
w_vert:
    ld (fs_vo),a
    ld hl,ht_uov
    add a,(hl)
    ld c,a
    ld a,(ht_uo)
    ld hl,WB + B_PX
    add hl,a
    ex de,hl
    ld a,c
    call ht_d24
    call fs_put
    ld a,(fs_vo)
    ld hl,ht_vov
    add a,(hl)
    ld c,a
    ld a,(ht_vo)
    ld hl,WB + B_PX
    add hl,a
    ex de,hl
    ld a,c
    call ht_d24
    jp fs_put

; cross2 for the edge F -> T from w: IX = wF (u, v), HL = wT. As cross_edge:
; A = 1 if > 0, 2 if < 0, 0 if 0; keeps s, |eu| + |ev|, |fu| + |fv| for ce_k
cross2w:
    ld (c2_t),hl
    ; eu = wF.u - wT.u, ev = wF.v - wT.v
    LDX32 0
    ld bc,(c2_t)
    call sub_bc
    ST32 ce_eu
    LDX32 4
    ld bc,(c2_t)
    inc bc
    inc bc
    inc bc
    inc bc
    call sub_bc
    ST32 ce_ev
    LDX32 0
    ST32 ce_fu
    LDX32 4
    ST32 ce_fv
    jp ce_tail

; the sign of cross2(e_u, e_v, f_u, f_v) for the edge from the point at HL
; (an au/bu/cu variable; its v follows 8 bytes on) to the one at DE:
; e = to - from, f = p - from; cross2 = (eu >> 2)(fv >> 2) - (ev >> 2)(fu >> 2).
; A = 1 if > 0, 2 if < 0, 0 if 0. port: also keeps, for edge ce_k (0-2), the
; value (ce_s), |eu| + |ev| (ce_e1) and |fu| + |fv| (ce_f1) for ht_edges' skip;
; the products are 16 x 16 when the halved values fit (exact either way)
cross_edge:
    ld (ce_from),hl
    ld (ce_to),de
    ex de,hl
    call ld32_hl
    ld bc,(ce_from)
    call sub_bc
    ST32 ce_eu                  ; eu
    ld hl,(ce_to)
    ld bc,8
    add hl,bc
    call ld32_hl
    ld bc,(ce_from)
    ld a,8
    add a,c
    ld c,a
    adc a,b
    sub c
    ld b,a
    call sub_bc
    ST32 ce_ev                  ; ev
    LD32 ht_pu
    ld bc,(ce_from)
    call sub_bc
    ST32 ce_fu                  ; fu
    LD32 ht_pv
    ld bc,(ce_from)
    ld a,8
    add a,c
    ld c,a
    adc a,b
    sub c
    ld b,a
    call sub_bc
    ST32 ce_fv                  ; fv
ce_tail:
    ; port: eu ev fu fv kept for edge k (ce_raw + 16 k): edge_far wants them
    ld a,(ce_k)
    add a,a
    add a,a
    ld (ce_k4),a
    add a,a
    add a,a
    ld hl,ce_raw
    add hl,a
    ex de,hl
    ld hl,ce_eu
    ld bc,16                    ; ce_eu ce_ev ce_fu ce_fv are together
    ldir
    ; the halved values, 16-bit if they fit
    ld hl,ce_eu
    call q2_at
    jr c,ce_slow
    ld (ce_a),hl
    ld hl,ce_ev
    call q2_at
    jr c,ce_slow
    ld (ce_b),hl
    ld hl,ce_fu
    call q2_at
    jr c,ce_slow
    ld (ce_c),hl
    ld hl,ce_fv
    call q2_at
    jr c,ce_slow
    ld de,(ce_a)
    call m_smul16               ; (eu >> 2)(fv >> 2)
    ST32 ce_t
    ld hl,(ce_b)
    ld de,(ce_c)
    call m_smul16               ; (ev >> 2)(fu >> 2)
    call m_neg32
    ADD32 ce_t
    jp ce_done
ce_slow:
    LD32 ce_eu
    SRA2
    ST32 ce_t
    LD32 ce_fv
    SRA2
    MUL32 ce_t
    ST32 ce_t
    LD32 ce_ev
    SRA2
    ST32 ce_b32
    LD32 ce_fu
    SRA2
    MUL32 ce_b32
    call m_neg32
    ADD32 ce_t
ce_done:
    push hl
    push de
    ld a,(ce_k4)
    ld bc,ce_s
    call st32_bc_a
    pop de
    pop hl
    ld a,d
    bit 7,a
    ld a,2
    ret nz
    ld a,d
    or e
    or h
    or l
    ret z
    ld a,1
    ret

; HL = (the long at HL) >> 2 in 16 bits; C if it is outside +-2^17
q2_at:
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    jp ht_q2

; DEHL = |the long at HL| + |the long at DE|
abs_sum:
    push de
    call ld32_hl
    bit 7,d
    call nz,m_neg32
    ST32 as_t
    pop hl
    call ld32_hl
    bit 7,d
    call nz,m_neg32
    ADD32 as_t
    ret

; port: Z (skip) if edge k (A: 0-2) can't make the contact: the ball is at
; least r + 48 from its line in the projection (u, v), so from the segment
; in 3D. cross2 times 16 is within 3 (|eu| + |ev| + |fu| + |fv|) + 9 of the
; exact eu fv - ev fu, and that over |e| (>= it over |eu| + |ev|) is the
; distance, so: 16 |s| - 3 (E1 + F1) - 9 >= (r + 48) E1 is enough
edge_far:
    add a,a
    add a,a
    ld (ce_k4),a
    ld hl,ce_s
    add hl,a
    call ld32_hl
    bit 7,d
    call nz,m_neg32
    ld a,d                      ; |s| >= 2^28: 16 |s| overflows, and is far more than enough
    and 0xF0
    jp nz,ef_yes
    SLA4                        ; 16 |s|
    ST32 ef_l
    ld a,(ce_k4)
    add a,a
    add a,a
    ld hl,ce_raw
    add hl,a
    ld (ef_p),hl
    ld de,4
    ex de,hl
    add hl,de
    ex de,hl                    ; HL = eu, DE = ev
    call abs_sum
    ST32 ef_e                   ; E1 = |eu| + |ev|
    ld hl,(ef_p)
    ld de,8
    add hl,de
    ld d,h
    ld e,l
    inc de
    inc de
    inc de
    inc de                      ; HL = fu, DE = fv
    call abs_sum
    ADD32 ef_e                  ; E1 + F1
    ld b,h
    ld c,l
    add hl,hl
    ex de,hl
    push bc
    ld b,h
    ld c,l
    adc hl,hl
    add hl,bc
    ex de,hl
    pop bc
    add hl,bc
    jr nc,ef_1
    inc de                      ; 3 (E1 + F1) (no overflow: E1, F1 < 2^20)
ef_1:
    ld bc,9
    add hl,bc
    jr nc,ef_2
    inc de
ef_2:
    ST32 ef_t
    LD32 ef_l
    SUB32 ef_t                  ; L = 16 |s| - 3 (E1 + F1) - 9
    jr c,ef_no                  ; negative
    ST32 ef_l
    LD32 ef_e
    ld bc,(WB + B_R)
    ld a,c
    add a,48
    ld c,a
    adc a,b
    sub c
    ld b,a
    call m_mul32s16             ; (r + 48) E1 (r + 48 < 32768)
    ST32 ef_t
    LD32 ef_l
    SUB32 ef_t
    jr c,ef_no
ef_yes:
    xor a
    ret
ef_no:
    ld a,1
    or a
    ret

; DEHL -= the long at (BC)
sub_bc:
    ld a,(bc)
    ld (m_t4),a
    inc bc
    ld a,(bc)
    ld (m_t4 + 1),a
    inc bc
    ld a,(bc)
    ld (m_t4 + 2),a
    inc bc
    ld a,(bc)
    ld (m_t4 + 3),a
    SUB32 m_t4
    ret

; the layout above assumes each u variable has its v 8 bytes on:
; ht_au, ht_bu, ht_cu at 0, 16, 32 and ht_av, ht_bv, ht_cv at 8, 24, 40.

; p - (the vertex at IY + A) for x, y, z into (fs_ptr), which moves on
fs_vert:
    ld (fs_vo),a
    ld de,WB + B_PX
    call ht_d24
    call fs_put
    ld a,(fs_vo)
    add a,3
    ld de,WB + B_PY
    call ht_d24
    call fs_put
    ld a,(fs_vo)
    add a,6
    ld de,WB + B_PZ
    call ht_d24
fs_put:
    ld bc,(fs_ptr)
    ld a,e
    ld (bc),a
    inc bc
    ld a,d
    ld (bc),a
    inc bc
    ld a,l
    ld (bc),a
    inc bc
    ld a,h
    ld (bc),a
    inc bc
    ld (fs_ptr),bc
    ret

; seg_closest for the segment a -> b with p - a at HL, p - b at DE (fs_pv
; entries) and the vertices at IY + A, IY + C (for ht_seg, the plain code,
; when a value doesn't fit the 16-bit products). phys.c's arithmetic:
; e = b - a = (p - a) - (p - b), q = a + (e t >> 12) = p - (p - a) + (e t >> 12)
fs_seg:
    ld (fs_pa),hl
    ld (fs_pb),de
    ld (fs_ao),a
    ld a,c
    ld (fs_bo),a
    ; the skip (ht_seg's notes): both ends beyond +m, or both beyond -m, on an axis
    ld b,3
fs_pre:
    push bc
    ld hl,(fs_pa)
    call fs_cls
    ld c,a
    ld hl,(fs_pb)
    call fs_cls
    pop de
    and c
    ret nz                      ; the same side (1 & 1 or 2 & 2)
    ld b,d
    ld hl,(fs_pa)
    inc hl
    inc hl
    inc hl
    inc hl
    ld (fs_pa),hl
    ld hl,(fs_pb)
    inc hl
    inc hl
    inc hl
    inc hl
    ld (fs_pb),hl
    djnz fs_pre
    ld hl,(_pc_segfull)
    inc hl
    ld (_pc_segfull),hl
    ; per axis: e (32), e >> 2 and (p - a) >> 2 (16, or the plain code)
    ld hl,(fs_pa)
    ld de,-12
    add hl,de
    ld (fs_pa),hl
    ld hl,(fs_pb)
    add hl,de
    ld (fs_pb),hl
    ld ix,fs_ax
    ld b,3
fs_e:
    push bc
    ld hl,(fs_pa)
    call ld32_hl
    ld (ix + 4),l               ; p - a
    ld (ix + 5),h
    ld (ix + 6),e
    ld (ix + 7),d
    ld bc,(fs_pb)
    call sub_bc                 ; e = (p - a) - (p - b)
    STX32 0
    ex de,hl                    ; HL:DE = e
    call ht_q2
    jr c,fs_slow
    ld (ix + 8),l               ; e >> 2
    ld (ix + 9),h
    ld l,(ix + 4)
    ld h,(ix + 5)
    ld e,(ix + 6)
    ld d,(ix + 7)
    ex de,hl
    call ht_q2
    jr c,fs_slow
    ld (ix + 10),l              ; (p - a) >> 2
    ld (ix + 11),h
    ld bc,12
    add ix,bc
    ld hl,(fs_pa)
    ld bc,4
    add hl,bc
    ld (fs_pa),hl
    ld hl,(fs_pb)
    add hl,bc
    ld (fs_pb),hl
    pop bc
    djnz fs_e
    jr fs_go
fs_slow:
    pop bc
    ld a,(fs_ao)
    ld c,a
    ld a,(fs_bo)
    ld b,a
    ld a,c
    ld c,b
    jp ht_seg
fs_go:
    ; den = sum (e >> 2)^2 >> 8, num = sum ((p - a) >> 2)(e >> 2) >> 8
    CLR32 sg_den
    CLR32 sg_num
    ld ix,fs_ax
    ld b,3
fs_dn:
    push bc
    ld l,(ix + 8)
    ld h,(ix + 9)
    ld d,h
    ld e,l
    call m_smul16
    ADD32 sg_den
    ST32 sg_den
    ld l,(ix + 10)
    ld h,(ix + 11)
    ld e,(ix + 8)
    ld d,(ix + 9)
    call m_smul16
    ADD32 sg_num
    ST32 sg_num
    ld bc,12
    add ix,bc
    pop bc
    djnz fs_dn
    LD32 sg_den
    SRA8
    ST32 sg_den
    LD32 sg_num
    SRA8
    ST32 sg_num
    call sg_tcalc
    ; d2 = sum (((p - a) - (e t >> 12)) >> 4)^2
    CLR32 sg_d2
    ld ix,fs_ax
    ld b,3
fs_d2:
    push bc
    LDX32 0
    ld bc,(sg_t)
    call m_mul32s16
    SRA12
    ld (ix + 12 * 3),l          ; e t >> 12 (fs_et)
    ld (ix + 12 * 3 + 1),h
    ld (ix + 12 * 3 + 2),e
    ld (ix + 12 * 3 + 3),d
    call m_neg32
    ld c,(ix + 4)
    ld b,(ix + 5)
    add hl,bc
    ex de,hl
    ld c,(ix + 6)
    ld b,(ix + 7)
    adc hl,bc
    ex de,hl
    SRA4
    call p_sq
    ADD32 sg_d2
    ST32 sg_d2
    ld bc,12
    add ix,bc
    pop bc
    djnz fs_d2
    ; if (d2 < bd) bd = d2, best = q = p - (p - a) + (e t >> 12)
    LD32 sg_d2
    CMP32 ht_bd
    ret nc
    ST32 ht_bd
    ld ix,fs_ax
    ld hl,WB + B_PX
    ld (fs_pa),hl
    ld hl,ht_best
    ld (fs_pb),hl
    ld b,3
fs_q:
    push bc
    ld hl,(fs_pa)
    call ld32_hl
    ld c,(ix + 4)
    ld b,(ix + 5)
    or a
    sbc hl,bc
    ex de,hl
    ld c,(ix + 6)
    ld b,(ix + 7)
    sbc hl,bc
    ex de,hl
    ld c,(ix + 12 * 3)
    ld b,(ix + 12 * 3 + 1)
    add hl,bc
    ex de,hl
    ld c,(ix + 12 * 3 + 2)
    ld b,(ix + 12 * 3 + 3)
    adc hl,bc
    ex de,hl
    ld bc,(fs_pb)
    call st32_bc
    ld hl,(fs_pa)
    ld bc,4
    add hl,bc
    ld (fs_pa),hl
    ld hl,(fs_pb)
    add hl,bc
    ld (fs_pb),hl
    ld bc,12
    add ix,bc
    pop bc
    djnz fs_q
    ret

; A = 1 if the long at HL is > m (sg_m), 2 if < -m, else 0 (m < 32768)
fs_cls:
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; HL = high word, DE = low
    ld a,h
    or l
    jr z,fc_lo
    bit 7,h
    jr nz,fc_neg
    ld a,1                      ; >= 65536 > m
    ret
fc_neg:
    ld a,h
    and l
    inc a
    jr z,fc_ffff
    ld a,2                      ; < -65536 < -m
    ret
fc_ffff:
    ; -65536 .. -1: < -m if low < 65536 - m
    ld hl,(sg_mm)
    ex de,hl
    or a
    sbc hl,de                   ; low - (65536 - m): C if below
    ld a,2
    ret c
    xor a
    ret
fc_lo:
    ; 0 .. 65535: > m if low > m
    ld hl,(sg_m)
    or a
    sbc hl,de                   ; m - low: C if low > m
    ld a,1
    ret c
    xor a
    ret

; seg_closest(b->p, A, C): vertices at IY + A, IY + C; keeps the nearest
; (ht_bd, ht_best). phys.c's arithmetic, with p - q worked out as
; (p - a) - (e t >> 12), which is the same number
ht_seg:
    ld (sg_ao),a
    ld a,c
    ld (sg_bo),a
    ; per axis: a, e = b - a, p - a; and the skip (port: a segment the ball is
    ; more than r + 16 from along some axis can't be the one that makes the
    ; contact: its d2 >= r2, see ht_edges). Results are the same.
    ld c,0
sg_pre:
    push bc
    ld a,(sg_ao)
    call add_voff
    call ldy24
    pop bc
    push bc
    ld a,c
    ld bc,sg_a
    call st32_bc_a              ; sg_a[k] = a
    pop bc
    push bc
    ld a,(sg_bo)
    call add_voff
    call ldy24
    ST32 sg_bv
    pop bc
    push bc
    ld hl,WB + B_PX
    ld a,c
    add hl,a
    call ld32_hl
    ST32 sg_pk
    SUB32 sg_bv
    ST32 sg_db                  ; p - b
    pop bc
    push bc
    ld a,c
    ld hl,sg_a
    add hl,a
    ld b,h
    ld c,l
    LD32 sg_pk
    call sub_bc                 ; p - a
    pop bc
    push bc
    push de
    push hl
    ld a,c
    ld bc,sg_p
    call st32_bc_a              ; sg_p[k] = p - a
    pop hl
    pop de
    ; p - a > m and p - b > m: skip; p - a < -m and p - b < -m: skip
    CMP32 sg_m
    jr c,sg_lo
    jr z,sg_lo
    LD32 sg_db
    CMP32 sg_m
    jr c,sg_pk2
    jr z,sg_pk2
    pop bc
    ret
sg_lo:
    CMP32 sg_mm
    jr nc,sg_pk2
    LD32 sg_db
    CMP32 sg_mm
    jr nc,sg_pk2
    pop bc
    ret
sg_pk2:
    ld hl,(_pc_segax)
    inc hl
    ld (_pc_segax),hl
    ; e = (p - a) - (p - b)
    pop bc
    push bc
    ld a,c
    ld hl,sg_p
    add hl,a
    call ld32_hl
    SUB32 sg_db
    pop bc
    push bc
    ld a,c
    ld bc,sg_e
    call st32_bc_a
    pop bc
    ld a,c
    add a,4
    ld c,a
    cp 12
    jp nz,sg_pre
    ; den = sum (e >> 2)^2 >> 8; num = sum (p >> 2)(e >> 2) >> 8
    ld hl,(_pc_segfull)
    inc hl
    ld (_pc_segfull),hl
    CLR32 sg_den
    CLR32 sg_num
    ld ix,sg_e
    ld b,3
sg_l2:
    push bc
    LDX32 0
    SRA2
    ST32 sg_e2
    call p_sq
    ADD32 sg_den
    ST32 sg_den
    LDX32 sg_p - sg_e
    SRA2
    MUL32 sg_e2
    ADD32 sg_num
    ST32 sg_num
    ld bc,4
    add ix,bc
    pop bc
    djnz sg_l2
    LD32 sg_den
    SRA8
    ST32 sg_den
    LD32 sg_num
    SRA8
    ST32 sg_num
    call sg_tcalc
    ; q = a + (e t >> 12); d2 = sum (((p - a) - (e t >> 12)) >> 4)^2
    CLR32 sg_d2
    ld ix,sg_e
    ld b,3
sg_l3:
    push bc
    LDX32 0
    ld bc,(sg_t)
    call m_mul32s16
    SRA12
    STX32 sg_et - sg_e          ; e t >> 12
    call m_neg32
    ld c,(ix + sg_p - sg_e)
    ld b,(ix + sg_p - sg_e + 1)
    add hl,bc
    ex de,hl
    ld c,(ix + sg_p - sg_e + 2)
    ld b,(ix + sg_p - sg_e + 3)
    adc hl,bc
    ex de,hl
    SRA4
    call p_sq
    ADD32 sg_d2
    ST32 sg_d2
    ld bc,4
    add ix,bc
    pop bc
    djnz sg_l3
    ; if (d2 < bd) bd = d2, best = q
    LD32 sg_d2
    CMP32 ht_bd
    ret nc
    ST32 ht_bd
    push iy
    ld ix,sg_a
    ld iy,ht_best
    ld b,3
sg_ql:
    push bc
    LDX32 0                     ; q = a + (e t >> 12)
    ld c,(ix + sg_et - sg_a)
    ld b,(ix + sg_et - sg_a + 1)
    add hl,bc
    ex de,hl
    ld c,(ix + sg_et - sg_a + 2)
    ld b,(ix + sg_et - sg_a + 3)
    adc hl,bc
    ex de,hl
    ld (iy + 0),l
    ld (iy + 1),h
    ld (iy + 2),e
    ld (iy + 3),d
    ld bc,4
    add ix,bc
    add iy,bc
    pop bc
    djnz sg_ql
    pop iy
    ret

; sg_t = den <= 0 ? 0 : num <= 0 ? 0 : num >= den ? FX : (num << 12) / den
sg_tcalc:
    ; t = den <= 0 ? 0 : num <= 0 ? 0 : num >= den ? FX : (num << 12) / den
    LD32 sg_den
    bit 7,d
    jp nz,sg_t0
    ld a,d
    or e
    or h
    or l
    jp z,sg_t0
    LD32 sg_num
    bit 7,d
    jp nz,sg_t0
    ld a,d
    or e
    or h
    or l
    jp z,sg_t0
    CMP32 sg_den
    jp nc,sg_t1
    ; num < den: a 12-bit quotient, unless num << 12 overflows (C wraps: then
    ; the plain division of the wrapped value)
    ld a,d
    or a
    jr nz,sg_wrap
    ld a,e
    cp 8
    jr nc,sg_wrap               ; num >= 2^19
    call sg_div12
    jr sg_tset
sg_wrap:
    SLA12
    DIV32 sg_den
    jr sg_tset
sg_t0:
    ld hl,0
    jr sg_tset
sg_t1:
    ld hl,FX
sg_tset:
    ld (sg_t),hl
    ret

; HL = (sg_num << 12) / sg_den for 0 < num < den, num < 2^19: twelve rounds
; of shift-and-subtract (the quotient is below 4096)
sg_div12:
    ld a,12
    ld (sd_n12),a
    ld ix,0                     ; the quotient
sd_l:
    add ix,ix
    LD32 sg_num                 ; r = r * 2
    add hl,hl
    ex de,hl
    adc hl,hl
    ex de,hl
    ST32 sg_num
    SUB32 sg_den
    jr c,sd_n                   ; r < den (no 32-bit overflow: r < 2 den)
    ST32 sg_num
    inc ix
sd_n:
    ld hl,sd_n12
    dec (hl)
    jr nz,sd_l
    push ix
    pop hl
    ret

; (BC + A) = DEHL
st32_bc_a:
    add a,c
    ld c,a
    adc a,b
    sub c
    ld b,a
    jp st32_bc

; A += C * 3 / 4: a 32-bit field offset (0, 4, 8) as a 24-bit one (0, 3, 6); B is lost
add_voff:
    push af
    ld a,c
    srl a
    srl a
    ld b,a
    ld a,c
    sub b
    ld b,a
    pop af
    add a,b
    ret

clr_vs:
    ld hl,0
    ld (c_vs),hl
    ld (c_vs + 2),hl
    ld (c_vs + 4),hl
    ld (c_vs + 6),hl
    ld (c_vs + 8),hl
    ld (c_vs + 10),hl
    ret

; hit_course(b = WB): the triangles of its 2-unit cell
p_hit_course:
    ; cx = (b->p.x - x0) >> 13; cz likewise
    LD32 WB + B_PX
    CMP32 _phy_gx0
    ret c                       ; b->p.x < g->x0
    SUB32 _phy_gx0
    SRA32 13
    ld a,d                      ; cx >= nx ?
    or e
    ret nz
    ld bc,(_phy_gnx)
    push hl
    or a
    sbc hl,bc
    pop hl
    ret nc
    ld (hc_cx),hl
    LD32 WB + B_PZ
    CMP32 _phy_gz0
    ret c
    SUB32 _phy_gz0
    SRA32 13
    ld a,d
    or e
    ret nz
    ld bc,(_phy_gnz)
    push hl
    or a
    sbc hl,bc
    pop hl
    ret nc
    ; cell = cz * nx + cx
    ld de,(_phy_gnx)
    call m_umul16
    ld de,(hc_cx)
    add hl,de
    ld (hc_cell),hl
    ld a,(odo_on)
    or a
    jp nz,hc_marble
    ; another ball: the cell's list, then hit_tri each
    ld ix,hc_buf
    call hc_fetch
    ret z
    ld b,a
    ld hl,hc_buf
hc_tri:
    push bc
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    push hl
    ex de,hl
    call p_hit_tri
    pop hl
    pop bc
    djnz hc_tri
    ret

; port: a ball with a cache slot (the marble, the first three enemies) keeps
; its cell's list (refs) and, for each triangle whose sphere test failed on
; an axis, by how much (slack, in dx units, at most 255) and the ball's
; odometer then (stamp, odo >> 4, 16 bits). The odometer adds up every move
; and push of the ball since (each changes an axis by at most |amount| + 1),
; so while slack >= (odo - stamp) / 16 + 4 that axis still fails (|dx|
; changes by at most that; +1 for the >> 4 of both) and the triangle is
; passed over: exactly what hit_tri would have done, without its work.
; Stamps are 16 bits: the slot is emptied when odo >> 4 crosses a multiple
; of 32768, so a difference is always below that.
hc_marble:
    ld ix,(cs_slot)
    ld l,(ix + CS_CELL)
    ld h,(ix + CS_CELL + 1)
    ld de,(hc_cell)
    or a
    sbc hl,de
    jr z,hcm_have
    ld (ix + CS_CELL),e
    ld (ix + CS_CELL + 1),d
    push ix
    ld bc,CS_REFS
    add ix,bc
    call hc_fetch
    pop ix
    ld (ix + CS_N),a
    or a
    ret z
    ld b,a                      ; nothing known yet
    push ix
    pop hl
    ld de,CS_SLACK
    add hl,de
hcm_z:
    ld (hl),0
    inc hl
    djnz hcm_z
hcm_have:
    ld a,(ix + CS_N)
    or a
    ret z
    ld (cs_n),a
    xor a
    ld (cs_k),a
    push ix
    pop hl
    ld de,CS_REFS
    add hl,de
    ld (cs_ref),hl
    push ix
    pop hl
    ld de,CS_SLACK
    add hl,de
    ld (cs_slk),hl
    push ix
    pop hl
    ld de,CS_STAMP
    add hl,de
    ld (cs_stp),hl
hcm_l:
    ld hl,(cs_slk)
    ld a,(hl)                   ; slack
    cp 4
    jr c,hcm_test
    ld c,a
    ; (odo >> 4) - stamp, 16 bits
    ld hl,(cs_stp)
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld hl,(odo4)
    or a
    sbc hl,de                   ; < 32768 (see above)
    ld de,4
    add hl,de
    ld a,h
    or a
    jr nz,hcm_test              ; the bound is past 255
    ld a,c
    cp l
    jr nc,hcm_next              ; slack >= the bound: passed over
hcm_test:
    ; test it; ht_ax16 notes a failed axis in slack and stamp
    ld hl,(cs_slk)
    ld (hl),0
    ld (mc_rec),hl
    ld hl,(cs_ref)
    ld e,(hl)
    inc hl
    ld d,(hl)
    ex de,hl
    call p_hit_tri
    ld hl,0
    ld (mc_rec),hl
hcm_next:
    ld hl,(cs_ref)
    inc hl
    inc hl
    ld (cs_ref),hl
    ld hl,(cs_slk)
    inc hl
    ld (cs_slk),hl
    ld hl,(cs_stp)
    inc hl
    inc hl
    ld (cs_stp),hl
    ld hl,cs_k
    inc (hl)
    ld a,(cs_n)
    cp (hl)
    jp nz,hcm_l
    ret

; the triangles of cell hc_cell into IX (16-bit refs): A = how many (Z if none)
hc_fetch:
    ld hl,(hc_cell)
    add hl,hl                   ; byte offset of off[cell] (< 64K)
    push hl
    call grid_word
    ld (hc_k0),de
    pop hl
    inc hl
    inc hl
    call grid_word
    ld hl,(hc_k0)
    ex de,hl
    or a
    sbc hl,de                   ; count
    ld a,l
    or a
    ret z
    ld (hc_n),a
    ld b,l
    ld hl,(hc_k0)
    add hl,hl                   ; byte offset into idx (< 64K)
hc_copy:
    push bc
    push hl
    call idx_word
    ld (ix + 0),e
    ld (ix + 1),d
    inc ix
    inc ix
    pop hl
    inc hl
    inc hl
    pop bc
    djnz hc_copy
    ld a,(hc_n)
    or a
    ret

; start using slot cs_slot for the ball in WB: cs_new = 1 means "a
; different ball than before": empty it; so does WB.p not being where the
; slot's ball was left. Loads its odometer, turns it on
cs_enter:
    ld ix,(cs_slot)
    ld a,(cs_new)
    or a
    jr nz,cse_new
    push ix
    pop hl
    ld de,CS_LASTP
    add hl,de
    ld de,WB + B_PX
    ld b,12
cse_cmp:
    ld a,(de)
    cp (hl)
    jr nz,cse_new
    inc hl
    inc de
    djnz cse_cmp
    jr cse_ok
cse_new:
    ld (ix + CS_CELL),0xFF
    ld (ix + CS_CELL + 1),0xFF
cse_ok:
    ld l,(ix + CS_ODO)
    ld h,(ix + CS_ODO + 1)
    ld (odo),hl
    ld l,(ix + CS_ODO + 2)
    ld h,(ix + CS_ODO + 3)
    ld (odo + 2),hl
    ld l,(ix + CS_ODO4)
    ld h,(ix + CS_ODO4 + 1)
    ld (odo4),hl
    ld a,1
    ld (odo_on),a
    ret

; done with the slot: its odometer and where the ball was left
cs_leave:
    ld a,(odo_on)
    or a
    ret z
    xor a
    ld (odo_on),a
    ld ix,(cs_slot)
    ld hl,(odo)
    ld (ix + CS_ODO),l
    ld (ix + CS_ODO + 1),h
    ld hl,(odo + 2)
    ld (ix + CS_ODO + 2),l
    ld (ix + CS_ODO + 3),h
    ld hl,(odo4)
    ld (ix + CS_ODO4),l
    ld (ix + CS_ODO4 + 1),h
    push ix
    pop hl
    ld de,CS_LASTP
    add hl,de
    ex de,hl
    ld hl,WB + B_PX
    ld bc,12
    ldir
    ret

; odo += |DEHL| + 1 (the current ball's odometer, while odo_on)
odo_add:
    ld a,(odo_on)
    or a
    ret z
    bit 7,d
    call nz,m_neg32
    ld bc,1
    add hl,bc
    jr nc,oa_1
    inc de
oa_1:
    ld bc,(odo)
    add hl,bc
    ld (odo),hl
    ex de,hl
    ld bc,(odo + 2)
    adc hl,bc
    ld (odo + 2),hl
    ; odo4 = odo >> 4 (16 bits); a new half of its range empties the slot
    ld de,(odo)
    ld b,4
    bsrl de,b
    ld a,l
    swapnib
    and 0xF0
    or d
    ld d,a
    ld a,(odo4 + 1)
    xor d
    ld (odo4),de
    and 0x80
    ret z
    ld ix,(cs_slot)
    ld (ix + CS_CELL),0xFF
    ld (ix + CS_CELL + 1),0xFF
    ret

; DE = the 16-bit word at byte offset HL in the grid's off[] pages
grid_word:
    ld a,(_phy_gpage)
    jr page_word
; ... in idx[]'s pages (HL = byte offset; idx is < 64K)
idx_word:
    ld a,(_phy_ipage)
page_word:
    ld b,a
    ld a,h
    rlca
    rlca
    rlca
    and 7
    add a,b
    nextreg 0x52,a
    ld a,h
    and 0x1F
    or 0x40
    ld h,a
    ld e,(hl)
    inc hl
    ld d,(hl)
    ret

; ======================================================================
; boxes
; ======================================================================

; to_box(o at IX, p at HL): bx_lx, bx_ly, bx_lz
p_to_box:
    push hl
    call ld32_hl
    ld c,(ix + O_CX)
    ld b,(ix + O_CX + 1)
    or a
    sbc hl,bc
    ex de,hl
    ld c,(ix + O_CX + 2)
    ld b,(ix + O_CX + 3)
    sbc hl,bc
    ex de,hl
    ST32 bx_dx                  ; dx = p.x - cx
    pop hl
    push hl
    ld bc,8
    add hl,bc
    call ld32_hl
    ld c,(ix + O_CZ)
    ld b,(ix + O_CZ + 1)
    or a
    sbc hl,bc
    ex de,hl
    ld c,(ix + O_CZ + 2)
    ld b,(ix + O_CZ + 3)
    sbc hl,bc
    ex de,hl
    ST32 bx_dz
    ; lx = ((dx >> 2) * cs - (dz >> 2) * sn) >> 12
    LD32 bx_dx
    SRA2
    ld c,(ix + O_CS)            ; cs, sn are Q14: 16-bit
    ld b,(ix + O_CS + 1)
    call m_mul32s16
    ST32 bx_t
    LD32 bx_dz
    SRA2
    ld c,(ix + O_SN)
    ld b,(ix + O_SN + 1)
    call m_mul32s16
    call m_neg32
    ADD32 bx_t
    SRA12
    ST32 bx_lx
    ; lz = ((dx >> 2) * sn + (dz >> 2) * cs) >> 12
    LD32 bx_dx
    SRA2
    ld c,(ix + O_SN)
    ld b,(ix + O_SN + 1)
    call m_mul32s16
    ST32 bx_t
    LD32 bx_dz
    SRA2
    ld c,(ix + O_CS)
    ld b,(ix + O_CS + 1)
    call m_mul32s16
    ADD32 bx_t
    SRA12
    ST32 bx_lz
    ; ly = p.y - cy
    pop hl
    ld bc,4
    add hl,bc
    call ld32_hl
    ld c,(ix + O_CY)
    ld b,(ix + O_CY + 1)
    or a
    sbc hl,bc
    ex de,hl
    ld c,(ix + O_CY + 2)
    ld b,(ix + O_CY + 3)
    sbc hl,bc
    ex de,hl
    ST32 bx_ly
    ret

; DEHL = clampf(DEHL, -h, h) with h at IX + A (h >= 0)
p_clamph:
    push hl
    push de
    push ix
    pop hl
    add hl,a
    call ld32_hl
    ST32 cl_h
    pop de
    pop hl
    CMP32 cl_h                  ; v > h: h
    jr c,clh_1
    jr z,clh_1
    LD32 cl_h
    ret
clh_1:
    ST32 cl_v
    LD32 cl_h
    call m_neg32
    ST32 cl_h
    LD32 cl_v
    CMP32 cl_h                  ; v < -h: -h
    ret nc
    LD32 cl_h
    ret

; int phys_overlap_obb(o at IX, p at HL, r = ov_r): A = 1/0
p_overlap:
    call p_to_box
    ; d = (l - clamp(l)) >> 4
    LD32 bx_lx
    ld a,O_HX
    call p_clamph
    call neg_add_lx
    SRA4
    call p_sq
    ST32 ov_s
    LD32 bx_ly
    ld a,O_HY
    call p_clamph
    call m_neg32
    ADD32 bx_ly
    SRA4
    call p_sq
    ADD32 ov_s
    ST32 ov_s
    LD32 bx_lz
    ld a,O_HZ
    call p_clamph
    call m_neg32
    ADD32 bx_lz
    SRA4
    call p_sq
    ADD32 ov_s
    ST32 ov_s
    ; < (r >> 4)^2
    LD32 ov_r
    SRA4
    call p_sq
    ST32 ov_rr
    LD32 ov_s
    CMP32 ov_rr
    ld a,0
    ret nc
    inc a
    ret

neg_add_lx:
    call m_neg32
    ADD32 bx_lx
    ret

; hit_box(b = WB, o at IX, vs = c_vs, cls = S_BOUNCY): A = 1 if touching
p_hit_box:
    ld hl,WB + B_PX
    call p_to_box
    LD32 bx_lx
    ld a,O_HX
    call p_clamph
    call neg_add_lx
    ST32 hb_dx
    LD32 bx_ly
    ld a,O_HY
    call p_clamph
    call m_neg32
    ADD32 bx_ly
    ST32 hb_dy
    LD32 bx_lz
    ld a,O_HZ
    call p_clamph
    call m_neg32
    ADD32 bx_lz
    ST32 hb_dz
    ; d2 = sum (d >> 4)^2; if (d2 >= (r >> 4)^2) return 0
    LD32 hb_dx
    SRA4
    call p_sq
    ST32 hb_d2
    LD32 hb_dy
    SRA4
    call p_sq
    ADD32 hb_d2
    ST32 hb_d2
    LD32 hb_dz
    SRA4
    call p_sq
    ADD32 hb_d2
    ST32 hb_d2
    LD32 WB + B_R
    SRA4
    call p_sq
    ST32 hb_r2
    LD32 hb_d2
    CMP32 hb_r2
    ld a,0
    ret nc
    ; d2 > 0 ?
    bit 7,d
    jp nz,hb_inside
    ld a,d
    or e
    or h
    or l
    jp z,hb_inside
    call m_isqrt
    SLA4
    ld a,d                      ; if (len < 16) len = 16
    or e
    or h
    jr nz,hb_l16
    ld a,l
    cp 16
    jr nc,hb_l16
    ld l,16
hb_l16:
    ST32 hb_len
    LD32 hb_dx
    SLA12
    DIV32 hb_len
    ld (hb_nx),hl
    LD32 hb_dy
    SLA12
    DIV32 hb_len
    ld (cn_y),hl
    LD32 hb_dz
    SLA12
    DIV32 hb_len
    ld (hb_nz),hl
    LD32 WB + B_R
    SUB32 hb_len
    ST32 c_pen
    jp hb_world
hb_inside:
    ; out through the nearest face: p* = h* - |l*|
    ld a,O_HX
    ld hl,bx_lx
    call hb_face
    ST32 hb_px
    ld a,O_HY
    ld hl,bx_ly
    call hb_face
    ST32 hb_py
    ld a,O_HZ
    ld hl,bx_lz
    call hb_face
    ST32 hb_pz
    ld hl,0
    ld (hb_nx),hl
    ld (cn_y),hl
    ld (hb_nz),hl
    ; if (py <= px && py <= pz) ny = +-FX, len = py + r
    LD32 hb_py
    CMP32 hb_px
    jr z,hbi_1
    jr nc,hbi_2
hbi_1:
    CMP32 hb_pz
    jr z,hbi_y
    jr nc,hbi_2
hbi_y:
    ld a,(bx_ly + 3)
    call hb_sign
    ld (cn_y),hl
    LD32 hb_py
    jr hbi_len
hbi_2:
    ; else if (px <= pz) nx, len = px + r; else nz, len = pz + r
    LD32 hb_px
    CMP32 hb_pz
    jr z,hbi_x
    jr nc,hbi_z
hbi_x:
    ld a,(bx_lx + 3)
    call hb_sign
    ld (hb_nx),hl
    LD32 hb_px
    jr hbi_len
hbi_z:
    ld a,(bx_lz + 3)
    call hb_sign
    ld (hb_nz),hl
    LD32 hb_pz
hbi_len:
    ADD32 WB + B_R
    ST32 c_pen
hb_world:
    ; back to world: wx = (nx cs + nz sn) >> 14, wz = (-nx sn + nz cs) >> 14
    ld hl,(hb_nx)
    ld e,(ix + O_CS)
    ld d,(ix + O_CS + 1)
    call m_smul16
    ST32 hb_t
    ld hl,(hb_nz)
    ld e,(ix + O_SN)
    ld d,(ix + O_SN + 1)
    call m_smul16
    ADD32 hb_t
    SRA32 14
    ld (cn_x),hl
    ld hl,(hb_nx)
    ld e,(ix + O_SN)
    ld d,(ix + O_SN + 1)
    call m_smul16
    call m_neg32
    ST32 hb_t
    ld hl,(hb_nz)
    ld e,(ix + O_CS)
    ld d,(ix + O_CS + 1)
    call m_smul16
    ADD32 hb_t
    SRA32 14
    ld (cn_z),hl
    ld a,S_BOUNCY
    ld (c_cls),a
    push ix
    call p_contact
    pop ix
    ld a,1
    ret

; DEHL = h - |l| (h at IX + A, l at (HL))
hb_face:
    push af
    call ld32_hl
    bit 7,d
    call nz,m_neg32
    call m_neg32
    ST32 hb_t
    pop af
    push ix
    pop hl
    add hl,a
    call ld32_hl
    ADD32 hb_t
    ret

; HL = -FX if A (a sign byte) is negative, else FX
hb_sign:
    ld hl,FX
    bit 7,a
    ret z
    ld hl,-FX
    ret

; ---- the moving world's boxes ----

; sweeper i (A) as an OBB at sw_obb (IX)
p_sweeper_box:
    ld (sw_i),a
    ld e,a
    ld d,SWEEP_SIZE
    mul d,e
    ld hl,_phy_sweep
    add hl,de
    ld (sw_p),hl
    ; a = s->yaw * 2 / 45 + (Wd.sweep_angle[i] >> 8)
    ld bc,20                    ; yaw
    add hl,bc
    call ld32_hl
    SLA32 1
    ld bc,c45
    call m_div32
    ST32 sw_a
    ld a,(sw_i)
    add a,a
    add a,a
    ld hl,_Wd + W_SWEEP
    add hl,a
    call ld32_hl
    SRA8
    ADD32 sw_a
    ST32 sw_a
    ; o: centre, hx = len / 2, hy = 717, hz = 922, cs = icos(a), sn = isin(a)
    ld hl,(sw_p)
    ld de,sw_obb
    ld bc,12
    ldir
    ld hl,(sw_p)
    ld bc,12
    add hl,bc
    call ld32_hl
    ld bc,c2
    call m_div32                ; len / 2 (C: truncates)
    ST32 sw_obb + O_HX
    LDI32 717
    ST32 sw_obb + O_HY
    LDI32 922
    ST32 sw_obb + O_HZ
    ld hl,(sw_a)
    call p_icosl
    ST32 sw_obb + O_CS
    ld hl,(sw_a)
    call p_isinl
    ST32 sw_obb + O_SN
    ld ix,sw_obb
    ret

; crusher i (A) as an OBB at sw_obb (IX)
p_crusher_box:
    ld (sw_i),a
    ld e,a
    ld d,CRUSH_SIZE
    mul d,e
    ld hl,_phy_crush
    add hl,de
    ld (sw_p),hl
    ld de,sw_obb
    ld bc,12
    ldir                        ; cx cy cz (cy fixed below)
    ; cy = c->y + (c->lift * Wd.crush_h[i] >> 12)
    ld a,(sw_i)
    add a,a
    add a,a
    ld hl,_Wd + W_CRUSHH
    add hl,a
    call ld32_hl
    ST32 sw_t
    ld hl,(sw_p)
    ld bc,24                    ; lift
    add hl,bc
    call ld32_hl
    MUL32 sw_t
    SRA12
    ADD32 sw_obb + O_CY
    ST32 sw_obb + O_CY
    ; hx = hz = w / 2, hy = 3686, cs, sn
    ld hl,(sw_p)
    ld bc,12
    add hl,bc
    call ld32_hl
    ld bc,c2
    call m_div32
    ST32 sw_obb + O_HX
    ST32 sw_obb + O_HZ
    LDI32 3686
    ST32 sw_obb + O_HY
    ld hl,(sw_p)
    ld bc,28
    add hl,bc
    ld de,sw_obb + O_CS
    ld bc,8
    ldir
    ld ix,sw_obb
    ret

; ======================================================================
; resets
; ======================================================================

; ball_init(IX = ball, p = bi_p, r, inv_mass, fric, bounce, gravity: bi_*)
p_ball_init:
    push ix
    pop de
    ld hl,bi_p
    ld bc,12
    ldir                        ; p
    xor a
    ld b,24
bi_z:
    ld (de),a                   ; v, w
    inc de
    djnz bi_z
    ld hl,bi_r
    ld bc,24
    ldir                        ; r inv_mass fric bounce gravity + rot_k slot (set below)
    ; rot_k = (FX * FX) / ((r * 1638) >> 12)
    LD32 bi_r
    MULC 1638
    SRA12
    ST32 bi_t
    LDI32 16777216
    DIV32 bi_t
    STX32 B_ROTK
    xor a
    ld (ix + B_GROUNDED),a
    ld (ix + B_GROUNDED + 1),a
    ld (ix + B_FROZEN),a
    ld (ix + B_FROZEN + 1),a
    ld (ix + B_LIVE),1
    ld (ix + B_LIVE + 1),a
    push ix
    pop hl
    ld bc,B_LGX
    add hl,bc
    ex de,hl
    ld hl,bi_p
    ld bc,12
    ldir
    xor a
    ld (ix + B_TENEMY),a
    ld (ix + B_TENEMY + 1),a
    ld (ix + B_TSWEEP),a
    ld (ix + B_TSWEEP + 1),a
    ld (ix + B_TTILE),a
    ld (ix + B_TTILE + 1),a
    ret

p_reset_enemies:
    ld a,(_Wd + W_NENEMY)
    or a
    ret z
    ld b,a
    ld ix,_Wd + W_ENEMY
pre_l:
    push bc
    ; steel marbles: mass 3, the deck's material; blobs glide
    push ix
    pop hl
    ld bc,E_HOMEX
    add hl,bc
    ld de,bi_p
    ld bc,12
    ldir
    ld hl,2867
    ld a,(ix + E_KIND)
    or a
    jr z,pre_k
    ld hl,2253
pre_k:
    ld (bi_r),hl
    ld hl,0
    ld (bi_r + 2),hl
    ld hl,1365
    ld (bi_r + 4),hl
    ld hl,2253
    ld (bi_r + 8),hl
    ld hl,492
    ld (bi_r + 12),hl
    ld hl,804
    ld (bi_r + 16),hl
    ld hl,0
    ld (bi_r + 6),hl
    ld (bi_r + 10),hl
    ld (bi_r + 14),hl
    ld (bi_r + 18),hl
    call p_ball_init
    xor a
    ld (ix + E_PHASE),a
    ld (ix + E_PHASE + 1),a
    ld (ix + E_PHASE + 2),a
    ld (ix + E_PHASE + 3),a
    ld bc,ENEMY_SIZE
    add ix,bc
    pop bc
    djnz pre_l
    ret

_phys_reset_enemies_b:
    push ix
    push iy
    call p_reset_enemies
    pop iy
    pop ix
    ret

_phys_reset_b:
    push ix
    push iy
    ; port: a new course: no triangle cache is valid
    ld hl,0xFFFF
    ld (cs_slots + 0 * CS_SIZE + CS_CELL),hl
    ld (cs_slots + 1 * CS_SIZE + CS_CELL),hl
    ld (cs_slots + 2 * CS_SIZE + CS_CELL),hl
    ld (cs_slots + 3 * CS_SIZE + CS_CELL),hl
    ; crumbles
    ld hl,_Wd + W_CRUMBLE
    ld de,_Wd + W_CRUMBLE + 1
    ld bc,32 * CRUMBLE_SIZE - 1
    ld (hl),0
    ldir
    ; enemies from their specs
    ld a,(_phy_nenemy)
    cp 9
    jr c,pr_n
    ld a,8
pr_n:
    ld (_Wd + W_NENEMY),a
    xor a
    ld (_Wd + W_NENEMY + 1),a
    ld a,(_Wd + W_NENEMY)
    or a
    jr z,pr_noe
    ld b,a
    ld ix,_Wd + W_ENEMY
    ld hl,_phy_enemy
pr_e:
    push bc
    ld a,(hl)                   ; kind (low byte)
    ld (ix + E_KIND),a
    ld (ix + E_KIND + 1),0
    inc hl
    inc hl
    inc hl
    inc hl
    push ix
    pop de
    push hl
    ld hl,E_HOMEX
    add hl,de
    ex de,hl
    pop hl
    ld bc,12
    ldir                        ; home
    push ix
    pop de
    push hl
    ld hl,E_RANGE
    add hl,de
    ex de,hl
    pop hl
    ld bc,16
    ldir                        ; range speed ax az
    ld bc,ENEMY_SIZE
    add ix,bc
    pop bc
    djnz pr_e
pr_noe:
    call p_reset_enemies
    ; sweepers, crushers
    ld hl,_Wd + W_SWEEP
    ld de,_Wd + W_SWEEP + 1
    ld bc,32 + 32 + 32 + 16 - 1 ; sweep_angle crush_h crush_clock crush_slammed
    ld (hl),0
    ldir
    ld a,(_phy_ncrush)
    or a
    jr z,pr_nc
    cp 9
    jr c,pr_c8
    ld a,8
pr_c8:
    ld b,a
    ld ix,_phy_crush
    ld iy,_Wd + W_CRUSHCLK
pr_c:
    push bc
    ; crush_clock[i] = (phase * period) >> 12
    LDX32 16                    ; period
    ST32 sw_t
    LDX32 20                    ; phase
    MUL32 sw_t
    SRA12
    ld (iy + 0),l
    ld (iy + 1),h
    ld (iy + 2),e
    ld (iy + 3),d
    ld bc,CRUSH_SIZE
    add ix,bc
    ld bc,4
    add iy,bc
    pop bc
    djnz pr_c
pr_nc:
    CLR32 _Wd + W_CLOCK
    pop iy
    pop ix
    ret

; ======================================================================
; the world step: obstacles and enemies
; ======================================================================
_phys_world_step_b:
    push ix
    push iy
    call p_world_step
    nextreg 0x52,SLOT2_HOME
    pop iy
    pop ix
    ret

p_world_step:
    ; Wd.clock++
    LD32 _Wd + W_CLOCK
    ld bc,1
    add hl,bc
    jr nc,ws_clk
    inc de
ws_clk:
    ST32 _Wd + W_CLOCK
    ; sweep_angle[i] += (speed * 233) >> 10
    ld a,(_phy_nsweep)
    or a
    jr z,ws_nosw
    ld b,a
    ld ix,_phy_sweep
    ld iy,_Wd + W_SWEEP
ws_sw:
    push bc
    LDX32 16                    ; speed
    MULC 233
    SRA32 10
    ld c,(iy + 0)
    ld b,(iy + 1)
    add hl,bc
    ex de,hl
    ld c,(iy + 2)
    ld b,(iy + 3)
    adc hl,bc
    ex de,hl
    ld (iy + 0),l
    ld (iy + 1),h
    ld (iy + 2),e
    ld (iy + 3),d
    ld bc,SWEEP_SIZE
    add ix,bc
    ld bc,4
    add iy,bc
    pop bc
    djnz ws_sw
ws_nosw:
    ; crushers
    ld a,(_phy_ncrush)
    or a
    jp z,ws_nocr
    xor a
ws_cr:
    ld (ws_i),a
    call ws_crusher
    ld a,(ws_i)
    inc a
    ld hl,_phy_ncrush
    cp (hl)
    jr c,ws_cr
ws_nocr:
    ; crumbling tiles
    ld a,(_phy_ncrumble)
    or a
    jr z,ws_nocb
    ld b,a
    ld ix,_Wd + W_CRUMBLE
ws_cb:
    push bc
    call ws_crumble
    ld bc,CRUMBLE_SIZE
    add ix,bc
    pop bc
    djnz ws_cb
ws_nocb:
    ; enemies
    ld a,(_Wd + W_NENEMY)
    or a
    ret z
    xor a
ws_en:
    ld (ws_i),a
    ld e,a
    ld d,ENEMY_SIZE
    mul d,e
    ld ix,_Wd + W_ENEMY
    add ix,de
    call ws_enemy
    ld a,(ws_i)
    inc a
    ld hl,_Wd + W_NENEMY
    cp (hl)
    jr c,ws_en
    ret

; one crusher (ws_i)
ws_crusher:
    ld e,a
    ld d,CRUSH_SIZE
    mul d,e
    ld hl,_phy_crush
    add hl,de
    ld (ws_c),hl
    ld bc,16
    add hl,bc
    call ld32_hl
    ST32 ws_period
    ; crush_clock[i] += 82; while (>= period) -= period
    ld a,(ws_i)
    add a,a
    add a,a
    ld hl,_Wd + W_CRUSHCLK
    add hl,a
    push hl
    call ld32_hl
    ld bc,82
    add hl,bc
    jr nc,wc_1
    inc de
wc_1:
    CMP32 ws_period
    jr c,wc_2
    SUB32 ws_period
    jr wc_1
wc_2:
    ST32 ws_t
    pop bc
    call st32_bc
    ; t = (clock << 12) / period
    LD32 ws_t
    SLA12
    DIV32 ws_period
    ST32 ws_t
    ld a,d
    or e
    jr nz,wc_drop
    ld bc,3072
    push hl
    or a
    sbc hl,bc
    pop hl
    jr nc,wc_drop
    ; t < 3072: x = (t << 12) / 3072; h = ((x * x >> 12) * (3 FX - 2 x)) >> 12
    SLA12
    ld bc,c3072
    call m_div32
    ST32 ws_x
    call p_sq
    SRA12
    ST32 ws_t
    LD32 ws_x
    SLA32 1
    call m_neg32
    ld bc,3 * FX
    add hl,bc
    jr nc,wc_3
    inc de
wc_3:
    MUL32 ws_t
    SRA12
    jr wc_h
wc_drop:
    ; x = ((t - 3072) << 12) / 1024; h = FX - (x * x >> 12)
    ld bc,3072
    or a
    sbc hl,bc
    jr nc,wc_4
    dec de
wc_4:
    SLA12
    SRA32 10                    ; / 1024: t - 3072 >= 0 here, so a shift is the same
    call p_sq
    SRA12
    call m_neg32
    ld bc,FX
    add hl,bc
    jr nc,wc_h
    inc de
wc_h:
    ST32 ws_h
    ld a,(ws_i)
    add a,a
    add a,a
    ld hl,_Wd + W_CRUSHH
    add hl,a
    ld b,h
    ld c,l
    LD32 ws_h
    call st32_bc
    ; low = h < 492
    LD32 ws_h
    ld bc,c492
    call m_cmp32
    ld a,(ws_i)
    add a,a
    ld hl,_Wd + W_SLAMMED
    add hl,a
    jr c,wc_low
    ld (hl),0                   ; else if (!low) slammed = 0
    inc hl
    ld (hl),0
    ret
wc_low:
    ld a,(hl)
    or a
    ret nz                      ; already slammed
    ld (hl),1
    ; anyone under it?
    xor a
    ld (ws_got),a
    ld a,(_phys_nmarbles)
    or a
    jr z,wc_thud
    ld b,a
    ld hl,_phys_marbles
wc_m:
    push bc
    push hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    push de
    pop iy                      ; m
    call wc_check
    pop hl
    inc hl
    inc hl
    pop bc
    djnz wc_m
    ld a,(ws_got)
    or a
    ret nz
wc_thud:
    ld a,SFX_THUD
    ld (_phys_sound),a
    xor a
    ld (_phys_sound + 1),a
    ret

; the crusher at ws_c against marble IY: if live, not frozen, |dy| < 6554 and close: CRUSHED
wc_check:
    ld a,(iy + B_LIVE)
    or (iy + B_LIVE + 1)
    ret z
    ld a,(iy + B_FROZEN)
    or (iy + B_FROZEN + 1)
    ret nz
    ; dy = m->p.y - c->y
    ld l,(iy + B_PY)
    ld h,(iy + B_PY + 1)
    ld e,(iy + B_PY + 2)
    ld d,(iy + B_PY + 3)
    ld bc,(ws_c)
    inc bc
    inc bc
    inc bc
    inc bc
    call sub_bc
    bit 7,d
    call nz,m_neg32
    ld a,d                      ; |dy| < 6554
    or e
    ret nz
    ld bc,6554
    push hl
    or a
    sbc hl,bc
    pop hl
    ret nc
    ; dx = (m->p.x - c->x) >> 4, dz likewise; hw = (c->w / 2 + 1638) >> 4
    ld l,(iy + B_PX)
    ld h,(iy + B_PX + 1)
    ld e,(iy + B_PX + 2)
    ld d,(iy + B_PX + 3)
    ld bc,(ws_c)
    call sub_bc
    SRA4
    call p_sq
    ST32 ws_t
    ld l,(iy + B_PZ)
    ld h,(iy + B_PZ + 1)
    ld e,(iy + B_PZ + 2)
    ld d,(iy + B_PZ + 3)
    ld bc,(ws_c)
    ld a,8
    add a,c
    ld c,a
    adc a,b
    sub c
    ld b,a
    call sub_bc
    SRA4
    call p_sq
    ADD32 ws_t
    ST32 ws_t
    ld hl,(ws_c)
    ld bc,12
    add hl,bc
    call ld32_hl
    ld bc,c2
    call m_div32
    ld bc,1638
    add hl,bc
    jr nc,wck_1
    inc de
wck_1:
    SRA4
    call p_sq
    ST32 ws_x                   ; hw * hw
    LD32 ws_t
    CMP32 ws_x
    ret nc
    ld hl,_str_crushed
    ld (iy + B_KILL),l
    ld (iy + B_KILL + 1),h
    ld a,1
    ld (ws_got),a
    ret

; one crumbling tile at IX
ws_crumble:
    ld a,(ix + CR_STATE)
    cp 1
    jr nz,wcb_2
    ; if (state == 1 && --timer <= 0) { state 2, timer 175, fall 0, THUD }
    call wcb_dec
    ret nc
    ld (ix + CR_STATE),2
    ld (ix + CR_TIMER),175
    xor a
    ld (ix + CR_TIMER + 1),a
    ld (ix + CR_TIMER + 2),a
    ld (ix + CR_TIMER + 3),a
    ld (ix + CR_FALL),a
    ld (ix + CR_FALL + 1),a
    ld (ix + CR_FALL + 2),a
    ld (ix + CR_FALL + 3),a
    ld a,SFX_THUD
    ld (_phys_sound),a
    xor a
    ld (_phys_sound + 1),a
    ret
wcb_2:
    cp 2
    ret nz
    ; fall += (175 - timer) * 9
    ld hl,175
    ld e,(ix + CR_TIMER)
    ld d,(ix + CR_TIMER + 1)
    or a
    sbc hl,de                   ; timer is 1..175 here: 16-bit
    ld d,h
    ld e,l
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,de
    ld e,(ix + CR_FALL)
    ld d,(ix + CR_FALL + 1)
    add hl,de
    ld (ix + CR_FALL),l
    ld (ix + CR_FALL + 1),h
    ld e,(ix + CR_FALL + 2)
    ld d,(ix + CR_FALL + 3)
    ld hl,0
    adc hl,de
    ld (ix + CR_FALL + 2),l
    ld (ix + CR_FALL + 3),h
    ; if (--timer <= 0) state = 0
    call wcb_dec
    ret nc
    ld (ix + CR_STATE),0
    ret

; --timer (long); C if it is now <= 0
wcb_dec:
    LDX32 CR_TIMER
    ld a,l
    or h
    jr nz,wcd_1
    dec de
wcd_1:
    dec hl
    STX32 CR_TIMER
    bit 7,d
    scf
    ret nz
    ld a,d
    or e
    or h
    or l
    scf
    ret z
    or a
    ret

; one enemy at IX
ws_enemy:
    ; the nearest live marble: m (IY or 0), bestd
    ld hl,0xFFFF
    ld (en_bestd),hl
    ld hl,0x7FFF
    ld (en_bestd + 2),hl
    ld hl,0
    ld (en_m),hl
    ld a,(_phys_nmarbles)
    or a
    jp z,en_found
    ld b,a
    ld hl,_phys_marbles
en_ml:
    push bc
    push hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (en_mk),de
    push de
    pop iy
    ; d = ((mk->p - e->b.p) >> 6)^2 summed
    ld a,B_PX
    call en_d6
    call p_sq
    ST32 en_d
    ld a,B_PZ
    call en_d6
    call p_sq
    ADD32 en_d
    ST32 en_d
    ld a,B_PY
    call en_d6
    call p_sq
    ADD32 en_d
    ST32 en_d
    ; if (!mk->live || mk->frozen) continue
    ld a,(iy + B_LIVE)
    or (iy + B_LIVE + 1)
    jr z,en_mn
    ld a,(iy + B_FROZEN)
    or (iy + B_FROZEN + 1)
    jr nz,en_mn
    LD32 en_d
    CMP32 en_bestd
    jr nc,en_mn
    ST32 en_bestd
    ld hl,(en_mk)
    ld (en_m),hl
en_mn:
    pop hl
    inc hl
    inc hl
    pop bc
    dec b
    jp nz,en_ml
en_found:
    ld a,(ix + E_KIND)
    cp 1
    jp z,en_blob
    ; steel: asleep when nobody's about and it's at rest
    ld hl,(en_m)
    ld a,h
    or l
    jr z,en_far
    LD32 en_bestd
    ld bc,c_far
    call m_cmp32                ; bestd > (40 * 64)^2 ?
    jr c,en_near
    jr z,en_near
en_far:
    ld a,(ix + B_GROUNDED)
    or (ix + B_GROUNDED + 1)
    jr z,en_near
    push ix
    pop hl
    ld bc,B_VX
    add hl,bc
    call p_vlen
    ld a,d
    or e
    jr nz,en_near
    ld bc,410
    or a
    sbc hl,bc
    ret c                       ; vlen(v) < 410: asleep
en_near:
    ld hl,(en_m)
    ld a,h
    or l
    jp z,en_drag
    push hl
    pop iy
    ; target = bestd <= (range >> 6)^2 ? m->p : e->home
    LDX32 E_RANGE
    SRA32 6
    call p_sq
    ST32 en_t
    LD32 en_bestd
    CMP32 en_t
    jr c,en_tm
    jr z,en_tm
    push ix
    pop hl
    ld bc,E_HOMEX
    add hl,bc
    jr en_tset
en_tm:
    push iy
    pop hl
en_tset:
    push hl
    call ld32_hl
    ld c,(ix + B_PX)
    ld b,(ix + B_PX + 1)
    or a
    sbc hl,bc
    ex de,hl
    ld c,(ix + B_PX + 2)
    ld b,(ix + B_PX + 3)
    sbc hl,bc
    ex de,hl
    ST32 en_tx
    pop hl
    ld bc,8
    add hl,bc
    call ld32_hl
    ld c,(ix + B_PZ)
    ld b,(ix + B_PZ + 1)
    or a
    sbc hl,bc
    ex de,hl
    ld c,(ix + B_PZ + 2)
    ld b,(ix + B_PZ + 3)
    sbc hl,bc
    ex de,hl
    ST32 en_tz
    ; l = vlen(tx, 0, tz)
    COPY32 en_v3, en_tx
    CLR32 en_v3 + 4
    COPY32 en_v3 + 8, en_tz
    ld hl,en_v3
    call p_vlen
    ST32 en_l
    ; if (l > 819) v += STEP(speed) * (t >> 4) / (l >> 4)
    ld bc,c819
    call m_cmp32
    jr c,en_drag
    jr z,en_drag
    LD32 en_l
    SRA4
    ST32 en_l
    LDX32 E_SPEED
    call p_step
    ST32 en_st
    LD32 en_tx
    SRA4
    MUL32 en_st
    DIV32 en_l
    ld bc,B_VX
    call add_ix
    LD32 en_tz
    SRA4
    MUL32 en_st
    DIV32 en_l
    ld bc,B_VZ
    call add_ix
en_drag:
    ; v.x -= v.x / 250; v.z -= v.z / 250; v.y -= gravity
    LDX32 B_VX
    ld bc,c250
    call m_div32
    call m_neg32
    ld bc,B_VX
    call add_ix
    LDX32 B_VZ
    ld bc,c250
    call m_div32
    call m_neg32
    ld bc,B_VZ
    call add_ix
    LDX32 B_GRAV
    call m_neg32
    ld bc,B_VY
    call add_ix
    ; port: enemies 0-2 have triangle caches (slots 1-3): see hc_marble
    push ix
    call ball_to_wb
    ld a,(ws_i)
    cp 3
    jr nc,en_nocache
    inc a
    ld e,a
    ld d,CS_SIZE
    mul d,e
    ld hl,cs_slots
    add hl,de
    ld (cs_slot),hl
    xor a                       ; an enemy's slot is its own
    ld (cs_new),a
    call cs_enter
en_nocache:
    ; p += STEP(v)
    LD32 WB + B_VX
    call p_step
    push hl
    push de
    call odo_add
    pop de
    pop hl
    ADD32 WB + B_PX
    ST32 WB + B_PX
    LD32 WB + B_VY
    call p_step
    push hl
    push de
    call odo_add
    pop de
    pop hl
    ADD32 WB + B_PY
    ST32 WB + B_PY
    LD32 WB + B_VZ
    call p_step
    push hl
    push de
    call odo_add
    pop de
    pop hl
    ADD32 WB + B_PZ
    ST32 WB + B_PZ
    ; contact_ground = 0; hit_course twice; grounded
    xor a
    ld (contact_ground),a
    call p_hit_course
    call p_hit_course
    ld a,(contact_ground)
    ld (WB + B_GROUNDED),a
    xor a
    ld (WB + B_GROUNDED + 1),a
    call cs_leave
    pop ix
    call wb_to_ball
    ; if (e->b.p.y < C.kill_y + 6 FX) back home
    LD32 _phy_kill_y
    ld bc,6 * FX
    add hl,bc
    jr nc,en_k1
    inc de
en_k1:
    ST32 en_t
    LDX32 B_PY
    CMP32 en_t
    ret nc
en_home:
    push ix
    pop hl
    ld bc,E_HOMEX
    add hl,bc
    push ix
    pop de
    ld bc,12
    ldir                        ; p = home
    push ix
    pop hl
    ld bc,B_VX
    add hl,bc
    ld b,24
en_hz:
    ld (hl),0                   ; v = w = 0
    inc hl
    djnz en_hz
    ret

; blob: slides across the course
en_blob:
    ld hl,(en_m)
    ld a,h
    or l
    jr z,eb_1
    ; phase += STEP((speed * 1434) >> 12)
    LDX32 E_SPEED
    MULC 1434
    SRA12
    call p_step
    ld bc,E_PHASE
    call add_ix
eb_1:
    ; a = (phase * 10430) >> 12; s = (isin(a) * 13107) >> 14
    LDX32 E_PHASE
    MULC 10430
    SRA12
    call p_isinl
    MULC 13107
    SRA32 14
    ST32 en_s
    ; p.x = home.x + (ax * s >> 14), p.z = home.z + (az * s >> 14), p.y = home.y
    LDX32 E_AX
    MUL32 en_s
    SRA32 14
    ld c,(ix + E_HOMEX)
    ld b,(ix + E_HOMEX + 1)
    add hl,bc
    ex de,hl
    ld c,(ix + E_HOMEX + 2)
    ld b,(ix + E_HOMEX + 3)
    adc hl,bc
    ex de,hl
    STX32 B_PX
    LDX32 E_AZ
    MUL32 en_s
    SRA32 14
    ld c,(ix + E_HOMEZ)
    ld b,(ix + E_HOMEZ + 1)
    add hl,bc
    ex de,hl
    ld c,(ix + E_HOMEZ + 2)
    ld b,(ix + E_HOMEZ + 3)
    adc hl,bc
    ex de,hl
    STX32 B_PZ
    LDX32 E_HOMEY
    STX32 B_PY
    ; eats any live marble it touches
    ld a,(_phys_nmarbles)
    or a
    ret z
    ld b,a
    ld hl,_phys_marbles
eb_m:
    push bc
    push hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    push de
    pop iy
    ld a,B_PX
    call en_d4
    call p_sq
    ST32 en_d
    ld a,B_PY
    call en_d4
    call p_sq
    ADD32 en_d
    ST32 en_d
    ld a,B_PZ
    call en_d4
    call p_sq
    ADD32 en_d
    ST32 en_d
    ; rr = (e->b.r + mk->r) >> 4
    LDX32 B_R
    ld c,(iy + B_R)
    ld b,(iy + B_R + 1)
    add hl,bc
    ex de,hl
    ld c,(iy + B_R + 2)
    ld b,(iy + B_R + 3)
    adc hl,bc
    ex de,hl
    SRA4
    call p_sq
    ST32 en_t
    ld a,(iy + B_LIVE)
    or (iy + B_LIVE + 1)
    jr z,eb_n
    ld a,(iy + B_FROZEN)
    or (iy + B_FROZEN + 1)
    jr nz,eb_n
    LD32 en_d
    CMP32 en_t
    jr nc,eb_n
    ld hl,_str_eaten
    ld (iy + B_KILL),l
    ld (iy + B_KILL + 1),h
eb_n:
    pop hl
    inc hl
    inc hl
    pop bc
    dec b
    jp nz,eb_m
    ret

; DEHL = (IY.field - IX.field) >> 6 / >> 4 for field offset A
en_d6:
    call en_dd
    SRA32 6
    ret
en_d4:
    call en_dd
    SRA4
    ret
en_dd:
    push iy
    pop hl
    add hl,a
    push af
    call ld32_hl
    pop af
    push ix
    pop bc
    add a,c
    ld c,a
    adc a,b
    sub c
    ld b,a
    jp sub_bc

; (IX + BC) += DEHL
add_ix:
    push ix
    ex (sp),hl
    add hl,bc
    ld b,h
    ld c,l
    pop hl
    ld a,(bc)
    add a,l
    ld (bc),a
    inc bc
    ld a,(bc)
    adc a,h
    ld (bc),a
    inc bc
    ld a,(bc)
    adc a,e
    ld (bc),a
    inc bc
    ld a,(bc)
    adc a,d
    ld (bc),a
    ret

; STEP(v) = (v * 1311) >> 16
p_step:
    MULC 1311
    SRA16
    ret

; the ball at IX -> WB, and back
ball_to_wb:
    push ix
    pop hl
    ld de,WB
    ld bc,BALL_SIZE
    ldir
    ret
wb_to_ball:
    push ix
    pop de
    ld hl,WB
    ld bc,BALL_SIZE
    ldir
    ret

; ======================================================================
; the marble against a steel marble (IX = the enemy; the marble is WB)
; ======================================================================
p_hit_enemy:
    ; d = m->p - e->b.p
    ld a,B_PX
    call he_d
    ST32 he_dx
    ld a,B_PY
    call he_d
    ST32 he_dy
    ld a,B_PZ
    call he_d
    ST32 he_dz
    ; len = vlen(d); rr = m->r + e->b.r
    ld hl,he_dx
    call p_vlen
    ST32 he_len
    LD32 WB + B_R
    ld c,(ix + B_R)
    ld b,(ix + B_R + 1)
    add hl,bc
    ex de,hl
    ld c,(ix + B_R + 2)
    ld b,(ix + B_R + 3)
    adc hl,bc
    ex de,hl
    ST32 he_rr
    ; if (len >= rr || len < 16) { touching_enemy = 0; return }
    LD32 he_len
    CMP32 he_rr
    jr nc,he_off
    bit 7,d
    jr nz,he_off
    ld a,d
    or e
    or h
    jr nz,he_on
    ld a,l
    cp 16
    jr nc,he_on
he_off:
    xor a
    ld (WB + B_TENEMY),a
    ld (WB + B_TENEMY + 1),a
    ret
he_on:
    ; n = d << 12 / len
    LD32 he_dx
    SLA12
    DIV32 he_len
    ld (cn_x),hl
    LD32 he_dy
    SLA12
    DIV32 he_len
    ld (cn_y),hl
    LD32 he_dz
    SLA12
    DIV32 he_len
    ld (cn_z),hl
    ; m->p += n * ((rr - len) * 3 / 4) >> 12; e->b.p.x, z -= n * ((rr - len) / 4) >> 12
    LD32 he_rr
    SUB32 he_len
    ST32 he_pen
    MULC 3
    ld bc,c4
    call m_div32
    ST32 he_t
    call odo_add                ; port: the marble's odometer
    ld hl,(cn_x)
    ld bc,WB + B_PX
    call he_addn
    ld hl,(cn_y)
    ld bc,WB + B_PY
    call he_addn
    ld hl,(cn_z)
    ld bc,WB + B_PZ
    call he_addn
    LD32 he_pen
    ld bc,c4
    call m_div32
    ST32 he_t
    ld hl,(cn_x)
    call he_muln
    call m_neg32
    ld bc,B_PX
    call add_ix
    ld hl,(cn_z)
    call he_muln
    call m_neg32
    ld bc,B_PZ
    call add_ix
    ; vn = dotn(m->v - e->b.v, n)
    ld a,B_VX
    call he_dv
    ST32 he_v
    ld a,B_VY
    call he_dv
    ST32 he_v + 4
    ld a,B_VZ
    call he_dv
    ST32 he_v + 8
    ld hl,he_v
    call p_dotn
    bit 7,d
    jp z,he_enter
    ST32 he_vn
    ; e_ = -vn > BOUNCE_MIN ? 614 : 0; j = (-vn - (vn * e_ >> 12)) * 3 / 4
    call m_neg32
    ST32 he_t                   ; -vn
    ld bc,c_bmin
    call m_cmp32                ; -vn <= 8192: e = 0
    ld bc,0
    jr c,he_e
    jr z,he_e
    ld bc,614
he_e:
    LD32 he_vn
    call m_mul32s16
    SRA12
    call m_neg32
    ADD32 he_t
    MULC 3
    ld bc,c4
    call m_div32
    ST32 he_j
    ; m->v += n * j >> 12
    ld hl,(cn_x)
    ld bc,WB + B_VX
    call he_addj
    ld hl,(cn_y)
    ld bc,WB + B_VY
    call he_addj
    ld hl,(cn_z)
    ld bc,WB + B_VZ
    call he_addj
    ; e->b.v -= (n * j / 3) >> 12
    ld hl,(cn_x)
    call he_j3
    ld bc,B_VX
    call add_ix
    ld hl,(cn_y)
    call he_j3
    ld bc,B_VY
    call add_ix
    ld hl,(cn_z)
    call he_j3
    ld bc,B_VZ
    call add_ix
he_enter:
    ; OnCollisionEnter: a shove
    ld a,(WB + B_TENEMY)
    or a
    jp nz,he_t1
    COPY32 he_v3, he_dx
    CLR32 he_v3 + 4
    COPY32 he_v3 + 8, he_dz
    ld hl,he_v3
    call p_vlen
    ST32 he_hl
    ld bc,c16
    call m_cmp32                ; hl > 16 ?
    jr c,he_snd
    jr z,he_snd
    LD32 he_dx
    SLA12
    DIV32 he_hl
    MULC 5
    ADD32 WB + B_VX
    ST32 WB + B_VX
    LD32 he_dz
    SLA12
    DIV32 he_hl
    MULC 5
    ADD32 WB + B_VZ
    ST32 WB + B_VZ
he_snd:
    ld a,SFX_CLACK
    ld (_phys_sound),a
    xor a
    ld (_phys_sound + 1),a
he_t1:
    ld a,1
    ld (WB + B_TENEMY),a
    xor a
    ld (WB + B_TENEMY + 1),a
    ret

; DEHL = WB.field - (IX + A).field
he_d:
    ld hl,WB
    add hl,a
    push af
    call ld32_hl
    pop af
    push ix
    pop bc
    add a,c
    ld c,a
    adc a,b
    sub c
    ld b,a
    jp sub_bc
he_dv:
    jr he_d

; (BC) += HL * he_t >> 12
he_addn:
    push bc
    ld b,h
    ld c,l
    LD32 he_t
    call m_mul32s16
    SRA12
    pop bc
    jp add_bc
; DEHL = HL * he_t >> 12
he_muln:
    ld b,h
    ld c,l
    LD32 he_t
    call m_mul32s16
    SRA12
    ret
; (BC) += HL * he_j >> 12
he_addj:
    push bc
    ld b,h
    ld c,l
    LD32 he_j
    call m_mul32s16
    SRA12
    pop bc
    jp add_bc
; DEHL = -((n * j / 3) >> 12)
he_j3:
    ld b,h
    ld c,l
    LD32 he_j
    call m_mul32s16
    ld bc,c3
    call m_div32
    SRA12
    jp m_neg32

; (BC) += DEHL
add_bc:
    ld a,(bc)
    add a,l
    ld (bc),a
    inc bc
    ld a,(bc)
    adc a,h
    ld (bc),a
    inc bc
    ld a,(bc)
    adc a,e
    ld (bc),a
    inc bc
    ld a,(bc)
    adc a,d
    ld (bc),a
    ret

; ======================================================================
; collide(b = WB)
; ======================================================================
p_collide:
    xor a
    ld (cl_swept),a
    ld (contact_ground),a
    ld (tile_touched),a
    ld a,2
cl_iter:
    ld (cl_it),a
    call p_hit_course
    ; pillars
    ld a,(_phy_npillar)
    or a
    jp z,cl_nopl
    ld b,a
    ld ix,_phy_pillar
cl_pl:
    push bc
    call cl_pillar
    ld bc,PILLAR_SIZE
    add ix,bc
    pop bc
    djnz cl_pl
cl_nopl:
    ; sweepers
    ld a,(_phy_nsweep)
    or a
    jr z,cl_nosw
    xor a
cl_sw:
    ld (cl_i),a
    call cl_sweeper
    ld a,(cl_i)
    inc a
    ld hl,_phy_nsweep
    cp (hl)
    jr c,cl_sw
cl_nosw:
    ; crushers
    ld a,(_phy_ncrush)
    or a
    jr z,cl_nocr
    xor a
cl_cr:
    ld (cl_i),a
    call p_crusher_box
    call clr_vs
    call p_hit_box
    ld a,(cl_i)
    inc a
    ld hl,_phy_ncrush
    cp (hl)
    jr c,cl_cr
cl_nocr:
    ld a,(cl_it)
    dec a
    jp nz,cl_iter
    ; if (!swept) b->touch_sweeper = 0
    ld a,(cl_swept)
    or a
    jr nz,cl_s1
    ld (WB + B_TSWEEP),a
    ld (WB + B_TSWEEP + 1),a
cl_s1:
    ; steel marbles
    ld a,(_Wd + W_NENEMY)
    or a
    jr z,cl_noen
    ld b,a
    ld ix,_Wd + W_ENEMY
cl_en:
    push bc
    ld a,(ix + E_KIND)
    or a
    call z,p_hit_enemy
    ld bc,ENEMY_SIZE
    add ix,bc
    pop bc
    djnz cl_en
cl_noen:
    ld a,(contact_ground)
    ld (WB + B_GROUNDED),a
    xor a
    ld (WB + B_GROUNDED + 1),a
    ; if (tile_touched) its crumble: state 1, timer 28
    ld a,(tile_touched)
    or a
    ret z
    ld (ht_group),a
    call crumble_state          ; HL = &state
    ld (hl),1
    inc hl
    ld (hl),0
    inc hl
    ld (hl),28
    inc hl
    xor a
    ld (hl),a
    inc hl
    ld (hl),a
    inc hl
    ld (hl),a
    ret

; one pillar at IX
cl_pillar:
    ; if (b->p.y < pl->y - b->r || b->p.y > pl->y + pl->h + b->r) continue
    LDX32 4
    SUB32 WB + B_R
    ST32 pl_t
    LD32 WB + B_PY
    CMP32 pl_t
    ret c
    LDX32 4
    ld c,(ix + 16)
    ld b,(ix + 17)
    add hl,bc
    ex de,hl
    ld c,(ix + 18)
    ld b,(ix + 19)
    adc hl,bc
    ex de,hl
    ST32 pl_top                 ; pl->y + pl->h
    ADD32 WB + B_R
    ST32 pl_t
    LD32 WB + B_PY
    CMP32 pl_t
    jr c,pl_1
    ret nz
pl_1:
    ; dx, dz; d2 = (dx >> 4)^2 + (dz >> 4)^2; rr = pl->r + b->r
    LD32 WB + B_PX
    ld c,(ix + 0)
    ld b,(ix + 1)
    or a
    sbc hl,bc
    ex de,hl
    ld c,(ix + 2)
    ld b,(ix + 3)
    sbc hl,bc
    ex de,hl
    ST32 pl_dx
    LD32 WB + B_PZ
    ld c,(ix + 8)
    ld b,(ix + 9)
    or a
    sbc hl,bc
    ex de,hl
    ld c,(ix + 10)
    ld b,(ix + 11)
    sbc hl,bc
    ex de,hl
    ST32 pl_dz
    LD32 pl_dx
    SRA4
    call p_sq
    ST32 pl_d2
    LD32 pl_dz
    SRA4
    call p_sq
    ADD32 pl_d2
    ST32 pl_d2
    LDX32 12
    ADD32 WB + B_R
    ST32 pl_rr
    SRA4
    call p_sq
    ST32 pl_t
    LD32 pl_d2
    CMP32 pl_t
    ret nc
    ; on top? b->p.y > top && d2 < (pl->r >> 4)^2
    LD32 pl_top
    CMP32 WB + B_PY
    jr nc,pl_side
    LDX32 12
    SRA4
    call p_sq
    ST32 pl_t
    LD32 pl_d2
    CMP32 pl_t
    jr nc,pl_side
    ; contact(b, 0, FX, 0, top + r - p.y, S_BOUNCY, still)
    LD32 pl_top
    ADD32 WB + B_R
    SUB32 WB + B_PY
    ST32 c_pen
    ld hl,0
    ld (cn_x),hl
    ld (cn_z),hl
    ld hl,FX
    ld (cn_y),hl
    jr pl_hit
pl_side:
    ; l = isqrt(d2) << 4, at least 16; contact(b, dx << 12 / l, 0, dz << 12 / l, rr - l)
    LD32 pl_d2
    call m_isqrt
    SLA4
    ld a,d
    or e
    or h
    jr nz,pl_lok
    ld a,l
    cp 16
    jr nc,pl_lok
    ld l,16
pl_lok:
    ST32 pl_l
    LD32 pl_dx
    SLA12
    DIV32 pl_l
    ld (cn_x),hl
    LD32 pl_dz
    SLA12
    DIV32 pl_l
    ld (cn_z),hl
    ld hl,0
    ld (cn_y),hl
    LD32 pl_rr
    SUB32 pl_l
    ST32 c_pen
pl_hit:
    ld a,S_BOUNCY
    ld (c_cls),a
    call clr_vs
    push ix
    call p_contact
    pop ix
    ret

; sweeper cl_i against the ball
cl_sweeper:
    call p_sweeper_box          ; IX = its box
    ; om = (speed * 71) >> 12; vs.x = (om * ((p.z - cz) >> 4)) >> 8; vs.z = -((om * ((p.x - cx) >> 4)) >> 8)
    ld hl,(sw_p)
    ld bc,16
    add hl,bc
    call ld32_hl
    MULC 71
    SRA12
    ST32 sw_om
    LD32 WB + B_PZ
    SUB32 sw_obb + O_CZ
    SRA4
    MUL32 sw_om
    SRA8
    ST32 c_vs
    CLR32 c_vs + 4
    LD32 WB + B_PX
    SUB32 sw_obb + O_CX
    SRA4
    MUL32 sw_om
    SRA8
    call m_neg32
    ST32 c_vs + 8
    call p_hit_box
    or a
    ret z
    ld a,1
    ld (cl_swept),a
    ld a,(WB + B_TSWEEP)
    or a
    ret nz
    ; the first touch: a shove of 7 away from the hub, a clack
    LD32 WB + B_PX
    SUB32 sw_obb + O_CX
    ST32 sw_v3
    CLR32 sw_v3 + 4
    LD32 WB + B_PZ
    SUB32 sw_obb + O_CZ
    ST32 sw_v3 + 8
    ld hl,sw_v3
    call p_vlen
    ST32 sw_hl
    ld bc,c16
    call m_cmp32
    jr c,sw_snd
    jr z,sw_snd
    LD32 sw_v3
    SLA12
    DIV32 sw_hl
    MULC 7
    ADD32 WB + B_VX
    ST32 WB + B_VX
    LD32 sw_v3 + 8
    SLA12
    DIV32 sw_hl
    MULC 7
    ADD32 WB + B_VZ
    ST32 WB + B_VZ
sw_snd:
    ld a,SFX_CLACK
    ld (_phys_sound),a
    xor a
    ld (_phys_sound + 1),a
    ld a,1
    ld (WB + B_TSWEEP),a
    xor a
    ld (WB + B_TSWEEP + 1),a
    ret

; ======================================================================
; phys_marble_step(Ball *b, fix wx, fix wz)
; ======================================================================
_phys_marble_step_b:
    push ix
    push iy
    ld ix,6
    add ix,sp
    ld l,(ix + 0)
    ld h,(ix + 1)
    ld (ms_ball),hl
    ld l,(ix + 2)
    ld h,(ix + 3)
    ld (ms_wx),hl
    ld l,(ix + 4)
    ld h,(ix + 5)
    ld (ms_wx + 2),hl
    ld l,(ix + 6)
    ld h,(ix + 7)
    ld (ms_wz),hl
    ld l,(ix + 8)
    ld h,(ix + 9)
    ld (ms_wz + 2),hl
    ld ix,(ms_ball)
    call ball_to_wb
    ; port: the marble's triangle cache (slot 0) is kept while nothing but
    ; this moves it (the same ball, where its last step left it)
    ld hl,cs_slots
    ld (cs_slot),hl
    ld hl,(ms_ball)
    ld de,(cs_ball)
    ld (cs_ball),hl
    or a
    sbc hl,de                   ; Z: the same ball as before
    ld a,1
    jr nz,msc_new
    xor a
msc_new:
    ld (cs_new),a
    call cs_enter
    call p_marble_step
    call cs_leave
    ld ix,(ms_ball)
    call wb_to_ball
    nextreg 0x52,SLOT2_HOME
    pop iy
    pop ix
    ret

p_marble_step:
    ld a,(WB + B_FROZEN)
    or a
    jr z,ms_go
    ld hl,WB + B_VX
    ld b,24
ms_fz:
    ld (hl),0
    inc hl
    djnz ms_fz
    ret
ms_go:
    ; input: a = grounded ? ACCEL : ACCEL * 1229 >> 12
    LD32 ms_wx
    ld a,h
    or l
    or d
    or e
    jr nz,ms_in
    LD32 ms_wz
    ld a,h
    or l
    or d
    or e
    jr z,ms_noin
ms_in:
    ld bc,ACCEL
    ld a,(WB + B_GROUNDED)
    or a
    jr nz,ms_acc
    ld bc,(ACCEL * 1229) >> 12
ms_acc:
    ld (ms_a),bc
    LD32 ms_wx
    call m_mul32s16
    SRA12
    ADD32 WB + B_VX
    ST32 WB + B_VX
    LD32 ms_wz
    ld bc,(ms_a)
    call m_mul32s16
    SRA12
    ADD32 WB + B_VZ
    ST32 WB + B_VZ
ms_noin:
    ; v.y -= gravity
    LD32 WB + B_VY
    SUB32 WB + B_GRAV
    ST32 WB + B_VY
    ; fans and boost strips
    ld a,(_phy_nzone)
    or a
    jp z,ms_nozone
    ld b,a
    ld ix,_phy_zone
ms_zone:
    push bc
    COPY32 ov_r, WB + B_R
    ld hl,WB + B_PX
    push ix
    call p_overlap
    pop ix
    or a
    jp z,ms_zn
    ; pw = STEP(power); v += dir * pw >> 14
    LDX32 OBB_SIZE + 12
    call p_step
    ST32 ms_pw
    LDX32 OBB_SIZE
    MUL32 ms_pw
    SRA32 14
    ADD32 WB + B_VX
    ST32 WB + B_VX
    LDX32 OBB_SIZE + 4
    MUL32 ms_pw
    SRA32 14
    ADD32 WB + B_VY
    ST32 WB + B_VY
    LDX32 OBB_SIZE + 8
    MUL32 ms_pw
    SRA32 14
    ADD32 WB + B_VZ
    ST32 WB + B_VZ
ms_zn:
    ld bc,ZONE_SIZE
    add ix,bc
    pop bc
    dec b
    jp nz,ms_zone
ms_nozone:
    ; cap the flat speed
    COPY32 ms_f, WB + B_VX
    CLR32 ms_f + 4
    COPY32 ms_f + 8, WB + B_VZ
    ld hl,ms_f
    call p_vlen
    ST32 ms_l
    ld bc,c_maxspeed
    call m_cmp32
    jr c,ms_nocap
    jr z,ms_nocap
    ; v = (v >> 4) * (MAXSPEED >> 4) / (l >> 4) << 4
    SRA4
    ST32 ms_l
    LD32 WB + B_VX
    SRA4
    MULC MAXSPEED >> 4
    DIV32 ms_l
    SLA4
    ST32 WB + B_VX
    LD32 WB + B_VZ
    SRA4
    MULC MAXSPEED >> 4
    DIV32 ms_l
    SLA4
    ST32 WB + B_VZ
ms_nocap:
    ; w -= w / 1000
    ld hl,WB + B_WX
    call ms_damp
    ld hl,WB + B_WY
    call ms_damp
    ld hl,WB + B_WZ
    call ms_damp
    ; sub = vlen(v) > 12 FX ? 2 : 1
    ld hl,WB + B_VX
    call p_vlen
    ld bc,c12fx
    call m_cmp32
    ld a,1
    jr c,ms_sub
    jr z,ms_sub
    inc a
ms_sub:
    ld (ms_sub_n),a
    ld (ms_k),a
ms_move:
    ; p += STEP(v) / sub (port: and each step on the odometer)
    LD32 WB + B_VX
    call ms_stepsub
    push hl
    push de
    call odo_add
    pop de
    pop hl
    ADD32 WB + B_PX
    ST32 WB + B_PX
    LD32 WB + B_VY
    call ms_stepsub
    push hl
    push de
    call odo_add
    pop de
    pop hl
    ADD32 WB + B_PY
    ST32 WB + B_PY
    LD32 WB + B_VZ
    call ms_stepsub
    push hl
    push de
    call odo_add
    pop de
    pop hl
    ADD32 WB + B_PZ
    ST32 WB + B_PZ
    call p_collide
    ld a,(ms_k)
    dec a
    ld (ms_k),a
    jr nz,ms_move
    ; if (grounded) last_ground = p
    ld a,(WB + B_GROUNDED)
    or a
    jr z,ms_ng
    ld hl,WB + B_PX
    ld de,WB + B_LGX
    ld bc,12
    ldir
ms_ng:
    ; acid
    COPY32 ov_r, WB + B_R
    ld a,(_phy_nacid)
    or a
    jr z,ms_noacid
    ld b,a
    ld ix,_phy_acid
ms_acid:
    push bc
    push ix
    ld hl,WB + B_PX
    call p_overlap
    pop ix
    or a
    jr z,ms_an
    ld hl,_str_dissolved
    ld (WB + B_KILL),hl
ms_an:
    ld bc,OBB_SIZE
    add ix,bc
    pop bc
    djnz ms_acid
ms_noacid:
    ; the goal
    ld ix,_phy_goal
    ld hl,WB + B_PX
    call p_overlap
    or a
    ret z
    ld a,1
    ld (WB + B_GOAL),a
    xor a
    ld (WB + B_GOAL + 1),a
    ret

; (HL) -= (HL) / 1000
ms_damp:
    push hl
    call ld32_hl
    ld bc,c1000
    call m_div32
    call m_neg32
    pop bc
    jp add_bc

; DEHL = STEP(DEHL) / sub
ms_stepsub:
    call p_step
    ld a,(ms_sub_n)
    cp 2
    ret nz
    ld bc,c2
    jp m_div32

; ======================================================================
; int phys_ground_below(const V3 *p, fix *y): the deck under a point (shadows).
; port: p by pointer (zsdcc passes no structures by value)
; ======================================================================
_phys_ground_below_b:
    push ix
    push iy
    ld ix,6
    add ix,sp
    ld l,(ix + 0)
    ld h,(ix + 1)
    ld de,gb_p
    ld bc,12
    ldir
    ld l,(ix + 2)
    ld h,(ix + 3)
    ld (gb_out),hl
    call p_ground_below
    nextreg 0x52,SLOT2_HOME
    ld l,a
    ld h,0
    pop iy
    pop ix
    ret

p_ground_below:
    xor a
    ld (gb_found),a
    CLR32 gb_best
    ; the cell
    LD32 gb_p
    CMP32 _phy_gx0
    jp c,gb_done
    SUB32 _phy_gx0
    SRA32 13
    ld a,d
    or e
    jp nz,gb_done
    ld bc,(_phy_gnx)
    push hl
    or a
    sbc hl,bc
    pop hl
    jp nc,gb_done
    ld (hc_cx),hl
    LD32 gb_p + 8
    CMP32 _phy_gz0
    jp c,gb_done
    SUB32 _phy_gz0
    SRA32 13
    ld a,d
    or e
    jp nz,gb_done
    ld bc,(_phy_gnz)
    push hl
    or a
    sbc hl,bc
    pop hl
    jp nc,gb_done
    ld de,(_phy_gnx)
    call m_umul16
    ld de,(hc_cx)
    add hl,de
    add hl,hl
    push hl
    call grid_word
    ld (hc_k0),de
    pop hl
    inc hl
    inc hl
    call grid_word
    ld hl,(hc_k0)
    ex de,hl
    or a
    sbc hl,de
    jp z,gb_done
    ld b,l
    ld hl,(hc_k0)
    add hl,hl
    ld ix,hc_buf
gb_copy:
    push bc
    push hl
    call idx_word
    ld (ix + 0),e
    ld (ix + 1),d
    inc ix
    inc ix
    pop hl
    inc hl
    inc hl
    pop bc
    djnz gb_copy
    push ix
    pop hl
    ld de,hc_buf
    or a
    sbc hl,de
    srl h
    rr l
    ld b,l
    ld hl,hc_buf
gb_tri:
    push bc
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    push hl
    ex de,hl
    call gb_one
    pop hl
    pop bc
    djnz gb_tri
gb_done:
    ld hl,(gb_out)
    ex de,hl
    ld hl,gb_best
    ld bc,4
    ldir
    ld a,(gb_found)
    ret

; one triangle for phys_ground_below
gb_one:
    call map_tri
    ; if (t->n[1] < 1024) continue
    ld l,(iy + T_N + 2)
    ld h,(iy + T_N + 3)
    ld bc,1024
    or a
    sbc hl,bc
    ret m                       ; (n.y is within +-4096: no overflow)
    ld a,(iy + T_GROUP)
    ld (ht_group),a
    or a
    jr z,gb_g
    call crumble_state
    cp 2
    ret z
gb_g:
    ; the x/z edge functions
    ld a,T_A
    ld (ht_uo),a                ; reuse: point offsets
    COPY32 ht_pu, gb_p
    COPY32 ht_pv, gb_p + 8
    ld a,T_A
    call ldy24
    ST32 ht_au
    ld a,T_A + 6
    call ldy24
    ST32 ht_av
    ld a,T_B
    call ldy24
    ST32 ht_bu
    ld a,T_B + 6
    call ldy24
    ST32 ht_bv
    ld a,T_C
    call ldy24
    ST32 ht_cu
    ld a,T_C + 6
    call ldy24
    ST32 ht_cv
    xor a
    ld (ce_k),a
    ld hl,ht_au
    ld de,ht_bu
    call cross_edge
    ld (ht_s1),a
    ld hl,ht_bu
    ld de,ht_cu
    call cross_edge
    ld (ht_s2),a
    ld hl,ht_cu
    ld de,ht_au
    call cross_edge
    ld b,a
    ld a,(ht_s1)
    or b
    ld b,a
    ld a,(ht_s2)
    or b
    cp 3
    ret z
    ; h = a->y - ((n0 * ((p.x - a->x) >> 2) + n2 * ((p.z - a->z) >> 2)) / n1 << 2)
    LD32 gb_p
    SUB32 ht_au
    SRA2
    ld c,(iy + T_N)
    ld b,(iy + T_N + 1)
    call m_mul32s16
    ST32 gb_t
    LD32 gb_p + 8
    SUB32 ht_av
    SRA2
    ld c,(iy + T_N + 4)
    ld b,(iy + T_N + 5)
    call m_mul32s16
    ADD32 gb_t
    ST32 gb_t
    ld l,(iy + T_N + 2)
    ld h,(iy + T_N + 3)
    ld de,0
    ST32 gb_n1
    LD32 gb_t
    DIV32 gb_n1
    SLA32 2
    call m_neg32
    ST32 gb_t
    ld a,T_A + 3
    call ldy24
    ADD32 gb_t
    ST32 gb_h
    ; if (h <= p.y + 256 && (!found || h > best)) best = h, found
    LD32 gb_p + 4
    inc h                       ; + 256
    jr nz,gb_1
    inc de
gb_1:
    ST32 gb_t
    LD32 gb_h
    CMP32 gb_t
    jr c,gb_2
    ret nz
gb_2:
    ld a,(gb_found)
    or a
    jr z,gb_take
    LD32 gb_h
    CMP32 gb_best
    ret c
    ret z
gb_take:
    COPY32 gb_best, gb_h
    ld a,1
    ld (gb_found),a
    ret

; the balls' triangle caches (hc_marble), in this bank: slot 0 the marble,
; 1-3 the first three enemies
defc CS_CELL = 0
defc CS_N = 2
defc CS_ODO = 3
defc CS_ODO4 = 7
defc CS_LASTP = 9
defc CS_REFS = 21
defc CS_SLACK = 21 + 200
defc CS_STAMP = 21 + 300
defc CS_SIZE = 21 + 500
cs_slots: defs 4 * CS_SIZE

; ======================================================================
SECTION rodata_user
qsin:
    defw 0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897
    defw 6270, 6639, 7005, 7366, 7723, 8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297
    defw 11585, 11866, 12140, 12406, 12665, 12916, 13160, 13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978
    defw 15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986, 16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384
surf_fric:   defw 2253, 4506, 205, 1229, 1024     ; deck .55 rough 1.1 ice .05 rail .3 bouncy .25
surf_bounce: defw 492, 82, 205, 1434, 2662
c2:     defq 2
c3:     defq 3
c4:     defq 4
c16:    defq 16
c45:    defq 45
c250:   defq 250
c492:   defq 492
c819:   defq 819
c1000:  defq 1000
c3072:  defq 3072
c_bmin: defq BOUNCE_MIN
c_maxspeed: defq MAXSPEED
c12fx:  defq 12 * 4096
c_far:  defq (40 * 64) * (40 * 64)
_str_crushed:   defm "CRUSHED", 0
_str_eaten:     defm "EATEN", 0
_str_dissolved: defm "DISSOLVED", 0

SECTION bss_user
_Wd:            defs WORLD_SIZE
_phys_sound:    defs 2
_phys_marbles:  defs 4
_phys_nmarbles: defs 2
_phy_goal:      defs OBB_SIZE
_phy_kill_y:    defs 4
_phy_nacid:     defs 1
_phy_acid:      defs 4 * OBB_SIZE
_phy_nzone:     defs 1
_phy_zone:      defs 4 * ZONE_SIZE
_phy_npillar:   defs 1
_phy_pillar:    defs 4 * PILLAR_SIZE
_phy_nsweep:    defs 1
_phy_sweep:     defs 4 * SWEEP_SIZE
_phy_ncrush:    defs 1
_phy_crush:     defs 4 * CRUSH_SIZE
_phy_nenemy:    defs 1
_phy_enemy:     defs 8 * ESPEC_SIZE
_phy_ncrumble:  defs 1
_phy_gx0:       defs 4
_phy_gz0:       defs 4
_phy_gnx:       defs 2
_phy_gnz:       defs 2
_phy_gpage:     defs 1
_phy_ipage:     defs 1
_phy_tpage:     defs 1

WB:             defs BALL_SIZE
_pc_tri: defs 2
_pc_sph: defs 2
_pc_plane: defs 2
_pc_edge: defs 2
_pc_segax: defs 2
_pc_segfull: defs 2
_pc_contact: defs 2
contact_ground: defs 1
tile_touched:   defs 1
cn_x:   defs 2
cn_y:   defs 2
cn_z:   defs 2
c_pen:  defs 4
c_cls:  defs 1
c_vs:   defs 12
c_rel:  defs 12
c_mvn:  defs 4
c_e:    defs 4
c_jn:   defs 4
c_rx:   defs 2
c_ry:   defs 2
c_rz:   defs 2
c_t:    defs 4
c_vc:   defs 12
c_vn:   defs 4
c_j:    defs 12
c_mj:   defs 4
c_jl:   defs 4
c_jl4:  defs 4
c_mj4:  defs 4
dn_s:   defs 4
dn_p:   defs 2
m_t4:   defs 4
ht_group: defs 1
ht_rr:  defs 4
ht_rr16: defs 2
ht_ax:  defs 2
ht_ay:  defs 2
ht_az:  defs 2
ax_lo:  defs 2
ht_dx:  defs 4
ht_dy:  defs 4
ht_dz:  defs 4
ht_s:   defs 4
ht_d:   defs 12
ht_dist: defs 4
ht_anx: defs 2
ht_any: defs 2
ht_anz: defs 2
ht_uo:  defs 1
ht_vo:  defs 1
ht_uov: defs 1
ht_vov: defs 1
ht_pu:  defs 4
ht_pad1: defs 4
ht_pv:  defs 4
; au/av, bu/bv, cu/cv: each v 8 bytes after its u (cross_edge)
ht_au:  defs 4
ht_pad2: defs 4
ht_av:  defs 4
ht_pad3: defs 4
ht_bu:  defs 4
ht_pad4: defs 4
ht_bv:  defs 4
ht_pad5: defs 4
ht_cu:  defs 4
ht_pad6: defs 4
ht_cv:  defs 4
ht_s1:  defs 1
ht_s2:  defs 1
ht_bd:  defs 4
ht_best: defs 12
ht_r2:  defs 4
ht_len: defs 4
ce_from: defs 2
ce_to:  defs 2
ce_eu:  defs 4
ce_ev:  defs 4
ce_fu:  defs 4
ce_fv:  defs 4                  ; (eu ev fu fv together: ce_tail)
ce_t:   defs 4
c2_t:   defs 2
ht_w:   defs 24                 ; p - a, p - b, p - c in (u, v)
ce_k:   defs 1
ce_k4:  defs 1
ce_a:   defs 2
ce_b:   defs 2
ce_c:   defs 2
ce_b32: defs 4
ce_s:   defs 12
ce_raw: defs 48                 ; eu ev fu fv for each edge
ef_p:   defs 2
as_t:   defs 4
ef_l:   defs 4
ef_e:   defs 4
ef_t:   defs 4
sg_ao:  defs 1
sg_bo:  defs 1
sg_e:   defs 12                 ; e = b - a (x y z)
sg_p:   defs 12                 ; p - a
sg_a:   defs 12                 ; a
sg_et:  defs 12                 ; e t >> 12
sg_db:  defs 4
sg_av:  defs 4
sg_bv:  defs 4
sg_pk:  defs 4
sg_da:  defs 4
sg_m:   defs 4
sg_mm:  defs 4
sg_e2:  defs 4
sg_den: defs 4
sg_num: defs 4
sg_t:   defs 2
sd_n12: defs 1
fs_ptr: defs 2
he_far: defs 3
fs_vo:  defs 1
fs_pa:  defs 2
fs_pb:  defs 2
fs_ao:  defs 1
fs_bo:  defs 1
fs_pv:  defs 36
fs_ax:  defs 36                 ; per axis: e (4), p - a (4), e >> 2 (2), (p - a) >> 2 (2)
fs_et:  defs 36                 ; e t >> 12 per axis, at fs_ax + 36 (the same stride)
sg_d2:  defs 4
hc_cx:  defs 2
hc_k0:  defs 2
hc_n:   defs 2
hc_buf: defs 2 * 128
hc_cell: defs 2
odo_on: defs 1
odo:   defs 4
cs_ball: defs 2
cs_new: defs 1
cs_slot: defs 2
cs_n:   defs 1
cs_k:   defs 1
cs_ref: defs 2
cs_slk: defs 2
cs_stp: defs 2
mc_rec: defs 2
odo4:   defs 2
bx_dx:  defs 4
bx_dz:  defs 4
bx_t:   defs 4
bx_lx:  defs 4
bx_ly:  defs 4
bx_lz:  defs 4
cl_h:   defs 4
cl_v:   defs 4
ov_r:   defs 4
ov_s:   defs 4
ov_rr:  defs 4
ov_p:   defs 12
hb_dx:  defs 4
hb_dy:  defs 4
hb_dz:  defs 4
hb_d2:  defs 4
hb_r2:  defs 4
hb_len: defs 4
hb_nx:  defs 2
hb_nz:  defs 2
hb_px:  defs 4
hb_py:  defs 4
hb_pz:  defs 4
hb_t:   defs 4
sw_i:   defs 1
sw_p:   defs 2
sw_a:   defs 4
sw_t:   defs 4
sw_om:  defs 4
sw_v3:  defs 12
sw_hl:  defs 4
sw_obb: defs OBB_SIZE
bi_p:   defs 12
bi_r:   defs 24                 ; r inv_mass fric bounce gravity (rot_k)
bi_t:   defs 4
ws_i:   defs 1
ws_c:   defs 2
ws_period: defs 4
ws_t:   defs 4
ws_x:   defs 4
ws_h:   defs 4
ws_got: defs 1
en_m:   defs 2
en_mk:  defs 2
en_bestd: defs 4
en_d:   defs 4
en_t:   defs 4
en_tx:  defs 4
en_tz:  defs 4
en_v3:  defs 12
en_l:   defs 4
en_st:  defs 4
en_s:   defs 4
he_dx:  defs 4
he_dy:  defs 4
he_dz:  defs 4
he_len: defs 4
he_rr:  defs 4
he_pen: defs 4
he_t:   defs 4
he_v:   defs 12
he_vn:  defs 4
he_j:   defs 4
he_v3:  defs 12
he_hl:  defs 4
cl_swept: defs 1
cl_it:  defs 1
cl_i:   defs 1
pl_t:   defs 4
pl_top: defs 4
pl_dx:  defs 4
pl_dz:  defs 4
pl_d2:  defs 4
pl_rr:  defs 4
pl_l:   defs 4
ms_ball: defs 2
ms_wx:  defs 4
ms_wz:  defs 4
ms_a:   defs 2
ms_pw:  defs 4
ms_f:   defs 12
ms_l:   defs 4
ms_sub_n: defs 1
ms_k:   defs 1
gb_p:   defs 12
gb_out: defs 2
gb_found: defs 1
gb_best: defs 4
gb_t:   defs 4
gb_n1:  defs 4
gb_h:   defs 4
