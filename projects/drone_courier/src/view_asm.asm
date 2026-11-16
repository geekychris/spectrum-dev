; view_asm.asm - view.c's hot path in Z80N assembly (port: zsdcc's C for
; these took most of a frame): a point's projection and outcode (prep), and
; a quad that needs no near clipping (quad: the cull, the polygon into the
; pool, its depth bucket). Quads with a corner behind the near plane go to
; view.c's quad_clip with the same arguments. The layouts are view.c's:
;
;   PV (11 bytes): x, y, z, sx, sy (int16), oc
;   DP (35 bytes): xy[16] (int16), n, col, next
;   pool at VIEW_RAM, then bhead[256], btail[256], pv[32] (bank 21)
;   recip: FOV * 4096 / z (uint16) for z < 4096 at 0x6000 (page RECIP_PAGE)

SECTION BANK_21

PUBLIC _prep, _quad, _tower_pts, _xform, _fastpoly
EXTERN l2p_fill, l2p_col, _l2p_recip
EXTERN _camx, _camy, _camz, _cy12, _sy12, _cp12, _sp12
EXTERN _ex_x, _ex_y, _ex_z, _ez_x, _ez_y, _ez_z, _g0_y, _g0_z
EXTERN _project_c, _quad_clip, _vw_npool
EXTERN l_z80n_muls_32_16x16

VIEW_RAM equ 0x2C00
POOL     equ 110
DP_SIZE  equ 35
BHEAD    equ VIEW_RAM + POOL * DP_SIZE
BTAIL    equ BHEAD + 256
PVS      equ BTAIL + 256
BUFA     equ PVS + 32 * 11         ; the fast fill's rows: chain A's x, chain B's x
BUFB     equ BUFA + 96
NIL      equ 255
NEARZ    equ 12
VCX      equ 128
VCY      equ 44
ROWS     equ 96
RECIP    equ 0x6000

OC_NEAR equ 0x01
OC_R    equ 0x02
OC_L    equ 0x04
OC_SL   equ 0x08
OC_SR   equ 0x10
OC_ST   equ 0x20
OC_SB   equ 0x40
OC_TODO equ 0x80

; ---------------------------------------------------------------- prep

; void prep(PV *p) __z88dk_fastcall: HL = p. Projects it (z >= NEARZ) and
; sets its outcode. Keeps IX.
_prep:
    push ix
    push hl
    pop ix
    call prep_ix
    pop ix
    ret

; IX = the point
prep_ix:
    ld l,(ix+4)
    ld h,(ix+5)                 ; HL = z
    bit 7,h
    jp nz,pr_near
    ld a,h
    or a
    jr nz,pr_far
    ld a,l
    cp NEARZ
    jp c,pr_near
    cp 24
    jr c,pr_c                   ; 12 <= z < 24: view.c (the scale passes 32767)
pr_far:
    ld a,h
    cp 0x10
    jr nc,pr_c                  ; z >= 4096: view.c (no table)
    ; s = recip[z]
    add hl,hl
    ld de,RECIP
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld (pr_s),hl
    ; sx = VCX + (x * s) >> 12
    ld e,(ix+0)
    ld d,(ix+1)
    call l_z80n_muls_32_16x16   ; DEHL = x * s
    add hl,hl
    rl e
    rl d
    add hl,hl
    rl e
    rl d
    add hl,hl
    rl e
    rl d
    add hl,hl
    rl e
    rl d                        ; DE = (x * s) >> 12
    ld hl,VCX
    add hl,de
    ld (ix+6),l
    ld (ix+7),h
    ; sy = VCY - (y * s) >> 13 (half the rows)
    ld hl,(pr_s)
    ld e,(ix+2)
    ld d,(ix+3)
    call l_z80n_muls_32_16x16
    add hl,hl
    rl e
    rl d
    add hl,hl
    rl e
    rl d
    add hl,hl
    rl e
    rl d                        ; DE = (y * s) >> 13
    ld hl,VCY
    or a
    sbc hl,de
    ld (ix+8),l
    ld (ix+9),h
    jr pr_screen
pr_c:
    push ix
    pop hl
    call _project_c             ; (fastcall: HL = the point)
pr_screen:
    ; the screen's sides: sx < 0, sx >= 256, sy < 0, sy >= ROWS
    ld c,0
    ld a,(ix+7)
    or a
    jr z,pr_xin
    ld c,OC_SL
    jp m,pr_xin
    ld c,OC_SR
pr_xin:
    ld a,(ix+9)
    or a
    jr z,pr_yhi
    jp p,pr_sb
    ld a,c
    or OC_ST
    ld c,a
    jr pr_frust
pr_yhi:
    ld a,(ix+8)
    cp ROWS
    jr c,pr_frust
pr_sb:
    ld a,c
    or OC_SB
    ld c,a
    jr pr_frust
pr_near:
    ld c,OC_NEAR
pr_frust:
    ; the engine's cull: x > z, x < -z
    ld l,(ix+0)
    ld h,(ix+1)                 ; HL = x
    ld e,(ix+4)
    ld d,(ix+5)                 ; DE = z
    push hl
    or a
    sbc hl,de                   ; x - z (no overflow: both well inside +-16384)
    pop hl
    jr z,pr_notr
    jp m,pr_notr
    ld a,c
    or OC_R
    ld c,a
    jr pr_done
pr_notr:
    add hl,de                   ; x + z < 0: x < -z
    bit 7,h
    jr z,pr_done
    ld a,c
    or OC_L
    ld c,a
pr_done:
    ld (ix+10),c
    ret

; ---------------------------------------------------------------- quad

; void quad(uint8_t ia, uint8_t ib, uint8_t ic, uint8_t id, int16_t bias, uint8_t col)
; (zsdcc pushes the chars as single bytes: sp+2..5 the points, +6 bias, +8 col)
_quad:
    ld hl,2
    add hl,sp
    ld de,q_args
    ld bc,7
    ldir                        ; q_args: ia ib ic id bias(2) col
    push ix
    ld hl,q_args
    ld de,q_ptr
    ld b,4
    ld c,0xFF                   ; all
    xor a
    ld (q_any),a
qd_pts:
    push bc
    ld a,(hl)                   ; index
    inc hl
    push hl
    push de
    ld e,a
    ld d,11
    mul de                      ; DE = index * 11
    ld hl,PVS
    add hl,de
    push hl
    pop ix                      ; IX = the point
    bit 7,(ix+10)
    call nz,prep_ix
    pop de
    ld a,ixl
    ld (de),a
    inc de
    ld a,ixh
    ld (de),a
    inc de
    pop hl
    pop bc
    ld a,(ix+10)
    and c
    ld c,a
    ld a,(q_any)
    or (ix+10)
    ld (q_any),a
    djnz qd_pts
    pop ix
    ; all of them behind, or beyond one side of the engine's cull
    ld a,c
    and OC_NEAR | OC_R | OC_L
    ret nz
    ld a,(_vw_npool)
    cp POOL
    ret z
    ld a,(q_any)
    and OC_NEAR
    jp nz,_quad_clip            ; a corner behind the near plane: view.c (same arguments)
    ld a,c
    and OC_SL | OC_SR | OC_ST | OC_SB
    ret nz                      ; all off one side of the screen
    ; the polygon: its four projections
    ld a,(_vw_npool)
    ld d,a
    ld e,DP_SIZE
    mul de
    ld hl,VIEW_RAM
    add hl,de
    ex de,hl                    ; DE = the pool's next
    push de
    ld hl,q_ptr
    ld b,4
qd_copy:
    push bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    push hl
    ld hl,6
    add hl,bc                   ; &p->sx
    ld bc,4
    ldir                        ; sx, sy
    pop hl
    pop bc
    djnz qd_copy
    ex de,hl
    ld bc,16
    add hl,bc                   ; &d->n (DE moved 16 on: back to the start + 32)
    ld a,(q_any)
    and OC_SL | OC_SR | OC_ST | OC_SB
    ld a,4
    jr nz,qd_n
    ld a,0x84                   ; all four on the screen: fastpoly
qd_n:
    ld (hl),a
    inc hl
    ld a,(q_args+6)
    ld (hl),a                   ; col
    inc hl
    ld (hl),NIL                 ; next
    pop de                      ; (the polygon, not needed: link by number)
    ; depth = (za + zb + zc + zd) >> 2 + bias
    ld hl,0
    ld (q_sum),hl
    ld de,q_ptr
    ld b,4
qd_z:
    ld a,(de)
    ld l,a
    inc de
    ld a,(de)
    ld h,a
    inc de
    push de
    ld de,4
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = z
    ld hl,(q_sum)
    add hl,de
    ld (q_sum),hl
    pop de
    djnz qd_z
    ld hl,(q_sum)
    sra h
    rr l
    sra h
    rr l
    ld de,(q_args+4)
    add hl,de                   ; depth
    ; bucket: < 0 -> 0, >= 4096 -> 255, else depth >> 4
    bit 7,h
    jr z,qd_pos
    xor a
    jr qd_bucket
qd_pos:
    ld a,h
    cp 0x10
    ld a,255
    jr nc,qd_bucket
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld a,h                      ; depth >> 4 (depth < 4096)
qd_bucket:
    ld hl,BHEAD
    add hl,a                    ; &bhead[b]
    ld a,(_vw_npool)
    ld c,a
    ld a,(hl)
    cp NIL
    jr nz,qd_append
    ld (hl),c                   ; the bucket's first
    jr qd_tail
qd_append:
    ; pool[btail[b]].next = npool
    push hl
    ld de,256
    add hl,de
    ld a,(hl)                   ; btail[b]
    ld d,a
    ld e,DP_SIZE
    mul de
    ld hl,VIEW_RAM + 34
    add hl,de
    ld (hl),c
    pop hl
qd_tail:
    inc h                       ; &btail[b] (bhead + 256)
    ld (hl),c
    inc c
    ld a,c
    ld (_vw_npool),a
    ret

; ---------------------------------------------------------------- xform

; void xform(int16_t wx, int16_t wy, int16_t wz, PV *o): the engine's
; cam_xform in whole units, each product >> 12 as it has them:
;   x = (dx c - dz s) >> 12, rz = (dx s + dz c) >> 12,
;   y = (dy cp + rz sp) >> 12, z = (rz cp - dy sp) >> 12; o->oc = OC_TODO
_xform:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    push hl
    ld hl,(_camx)
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
    ld hl,(_camy)
    ex de,hl
    or a
    sbc hl,de
    ld (xf_dy),hl
    pop hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    push hl
    ld hl,(_camz)
    ex de,hl
    or a
    sbc hl,de
    ld (xf_dz),hl
    pop hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (xf_o),de
    ; x = (dx c - dz s) >> 12
    ld hl,(xf_dx)
    ld de,(_cy12)
    call l_z80n_muls_32_16x16
    ld (xf_p),hl
    ld (xf_p+2),de
    ld hl,(xf_dz)
    ld de,(_sy12)
    call l_z80n_muls_32_16x16
    call xf_sub                 ; HL = (p - DEHL) >> 12
    ld de,(xf_o)
    ex de,hl
    ld (hl),e
    inc hl
    ld (hl),d
    ; rz = (dx s + dz c) >> 12
    ld hl,(xf_dx)
    ld de,(_sy12)
    call l_z80n_muls_32_16x16
    ld (xf_p),hl
    ld (xf_p+2),de
    ld hl,(xf_dz)
    ld de,(_cy12)
    call l_z80n_muls_32_16x16
    call xf_add
    ld (xf_rz),hl
    ; y = (dy cp + rz sp) >> 12
    ld hl,(xf_dy)
    ld de,(_cp12)
    call l_z80n_muls_32_16x16
    ld (xf_p),hl
    ld (xf_p+2),de
    ld hl,(xf_rz)
    ld de,(_sp12)
    call l_z80n_muls_32_16x16
    call xf_add
    ld de,(xf_o)
    ex de,hl
    inc hl
    inc hl
    ld (hl),e
    inc hl
    ld (hl),d
    ; z = (rz cp - dy sp) >> 12
    ld hl,(xf_rz)
    ld de,(_cp12)
    call l_z80n_muls_32_16x16
    ld (xf_p),hl
    ld (xf_p+2),de
    ld hl,(xf_dy)
    ld de,(_sp12)
    call l_z80n_muls_32_16x16
    call xf_sub
    ld de,(xf_o)
    ex de,hl
    ld bc,4
    add hl,bc
    ld (hl),e
    inc hl
    ld (hl),d
    ld bc,5
    add hl,bc                   ; &o->oc (z's high byte + 5)
    ld (hl),OC_TODO
    ret

; HL = (xf_p - DEHL) >> 12, or (xf_p + DEHL) >> 12 (low 16 bits)
xf_sub:
    ld b,h
    ld c,l
    ld hl,(xf_p)
    or a
    sbc hl,bc
    push hl
    ld hl,(xf_p+2)
    sbc hl,de
    ex de,hl
    pop hl
    jr xf_shift
xf_add:
    ld b,h
    ld c,l
    ld hl,(xf_p)
    add hl,bc
    push hl
    ld hl,(xf_p+2)
    adc hl,de
    ex de,hl
    pop hl
xf_shift:                       ; DEHL >> 12 -> HL: bits 12..27
    add hl,hl
    rl e
    rl d
    add hl,hl
    rl e
    rl d
    add hl,hl
    rl e
    rl d
    add hl,hl
    rl e
    rl d
    ex de,hl
    ret

; ---------------------------------------------------------------- tower corners

; void tower_pts(uint8_t c, uint8_t r, int16_t hy, int16_t hz)
; pv[0..3] the roof's corners (x0 z0, x1 z0, x1 z1, x0 z1), pv[4..7] the
; ground's: each coordinate the sum of its column edge's, row edge's and
; height's terms (16ths), >> 4; all to be prepared (OC_TODO)
_tower_pts:
    ld hl,2
    add hl,sp
    ld a,(hl)
    add a,a
    ld (tp_ex),a                ; 2c
    inc hl
    ld a,(hl)
    add a,a
    ld (tp_ez),a                ; 2r
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (tp_hy),de
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (tp_hz),de
    push ix
    ld ix,PVS
    ld b,0                      ; corner k
tp_loop:
    push bc
    ; ex = 2c + (k == 1 || k == 2), ez = 2r + (k >= 2): offsets into the word arrays
    ld a,b
    dec a
    cp 2
    ld a,(tp_ex)
    adc a,0                     ; carry if k - 1 < 2: k = 1, 2
    add a,a
    ld (tp_oe),a
    ld a,b
    cp 2
    ccf
    ld a,(tp_ez)
    adc a,0                     ; carry if k >= 2
    add a,a
    ld (tp_oz),a
    ; x
    ld hl,_ex_x
    ld de,_ez_x
    call tp_sum                 ; HL = ex_x[ex] + ez_x[ez]
    call tp_sr4
    ld (ix+0),l
    ld (ix+1),h
    ld (ix+44),l                ; the ground corner: pv[k + 4] = 44 bytes on
    ld (ix+45),h
    ; y
    ld hl,_ex_y
    ld de,_ez_y
    call tp_sum
    push hl
    ld de,(tp_hy)
    add hl,de
    call tp_sr4
    ld (ix+2),l
    ld (ix+3),h
    pop hl
    ld de,(_g0_y)
    add hl,de
    call tp_sr4
    ld (ix+46),l
    ld (ix+47),h
    ; z
    ld hl,_ex_z
    ld de,_ez_z
    call tp_sum
    push hl
    ld de,(tp_hz)
    add hl,de
    call tp_sr4
    ld (ix+4),l
    ld (ix+5),h
    pop hl
    ld de,(_g0_z)
    add hl,de
    call tp_sr4
    ld (ix+48),l
    ld (ix+49),h
    ld (ix+10),OC_TODO
    ld (ix+54),OC_TODO
    ld de,11
    add ix,de
    pop bc
    inc b
    ld a,b
    cp 4
    jp nz,tp_loop
    pop ix
    ret

; HL = HL[tp_oe] + DE[tp_oz] (int16 arrays)
tp_sum:
    ld a,(tp_oe)
    add hl,a
    ld c,(hl)
    inc hl
    ld b,(hl)
    ex de,hl
    ld a,(tp_oz)
    add hl,a
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    add hl,bc
    ret

; HL >>= 4 (arithmetic)
tp_sr4:
    sra h
    rr l
    sra h
    rr l
    sra h
    rr l
    sra h
    rr l
    ret

; ---------------------------------------------------------------- the fast fill

; void fastpoly(const DP *d) __z88dk_fastcall: a convex quad wholly on the
; screen (0..255, 0..ROWS-1), filled as l2poly fills it: rows ytop <= y <
; ybot, each from the lower to the higher of its two chains' x; chain A
; forward from the top vertex, B backward; each edge from its upper vertex
; at x + 0.5, stepping its slope (dx * l2p_recip[dy], 16.16) in 16.8. The
; edges are stepped into two row buffers, then every row goes to l2poly's
; l2p_fill (its page mapping and span stores): no clipping, no chains to
; switch per row.
_fastpoly:
    push ix
    ; the vertices as bytes: fp_x[4], fp_y[4]; the colour
    ld de,fp_x
    ld b,4
fp_ld:
    ld a,(hl)
    ld (de),a                   ; x (0..255)
    inc hl
    inc hl
    ld a,(hl)                   ; y (0..ROWS-1)
    push hl
    ld hl,4
    add hl,de                   ; &fp_y[k]
    ld (hl),a
    pop hl
    inc de
    inc hl
    inc hl
    djnz fp_ld
    ld de,16 + 1
    add hl,de                   ; xy[16] ends at +32: n at +32, col at +33
    ld a,(hl)
    ld (l2p_col),a
    ; the top (the first vertex of least y) and the bottom row
    ld hl,fp_y
    ld a,(hl)
    ld (fp_ymin),a
    ld (fp_ymax),a
    xor a
    ld (fp_itop),a
    ld c,1
fp_find:
    inc hl
    ld a,(fp_ymin)
    cp (hl)
    jr c,fp_notmin
    jr z,fp_notmin
    ld a,(hl)
    ld (fp_ymin),a
    ld a,c
    ld (fp_itop),a
fp_notmin:
    ld a,(fp_ymax)
    cp (hl)
    jr nc,fp_notmax
    ld a,(hl)
    ld (fp_ymax),a
fp_notmax:
    inc c
    ld a,c
    cp 4
    jr nz,fp_find
    ld a,(fp_ymin)
    ld b,a
    ld a,(fp_ymax)
    cp b
    jp z,fp_done                ; flat
    ; chain A forward into BUFA, chain B backward into BUFB
    ld hl,BUFA
    ld (fp_buf),hl
    ld a,1
    call fp_chain
    ld hl,BUFB
    ld (fp_buf),hl
    ld a,3                      ; (-1 mod 4)
    call fp_chain
    ; the rows
    ld a,(fp_ymin)
fp_row:
    ld (fp_y0),a
    ld b,a
    ld hl,BUFA
    add hl,a
    ld e,(hl)
    ld hl,BUFB
    add hl,a
    ld l,(hl)
    ld a,e
    cp l
    jr c,fp_ord
    ld e,l
    ld l,a                      ; E = left, L = right
fp_ord:
    ld d,0
    ld h,0
    call l2p_fill               ; B = y, DE..HL (keeps IX)
    ld a,(fp_ymax)
    ld b,a
    ld a,(fp_y0)
    inc a
    cp b
    jr nz,fp_row
fp_done:
    pop ix
    ret

; one chain: from the top vertex, A = the index step (1 or 3), its edges
; going down into (fp_buf) until a vertex on the bottom row
fp_chain:
    ld (fp_dir),a
    ld a,(fp_itop)
    ld (fp_i),a
    ld b,4                      ; at most 4 edges
fp_edge:
    push bc
    ld a,(fp_i)
    ld c,a                      ; v
    ld a,(fp_dir)
    add a,c
    and 3
    ld (fp_i),a                 ; w
    ld hl,fp_y
    add hl,a
    ld d,(hl)                   ; D = w.y
    ld hl,fp_x
    add hl,a
    ld e,(hl)                   ; E = w.x
    ld a,c
    ld hl,fp_y
    add hl,a
    ld a,d
    sub (hl)                    ; dy = w.y - v.y
    jp z,fp_next                ; level: nothing
    jp c,fp_end                 ; going up (not convex here): stop
    ld (fp_dy),a
    ld a,(hl)
    ld (fp_vy),a
    ld a,c
    ld hl,fp_x
    add hl,a
    ld a,(hl)
    ld (fp_vx),a
    ; dx = w.x - v.x (signed 16)
    ld l,e
    ld h,0
    ld e,a
    ld d,0
    or a
    sbc hl,de                   ; HL = dx
    ; the slope (16.16): dy 1: dx << 16; 2: dx << 15; else dx * recip[dy] (l2poly's)
    ld a,(fp_dy)
    cp 1
    jr z,fp_dy1
    cp 2
    jr z,fp_dy2
    push hl
    ld hl,_l2p_recip
    add hl,a
    add hl,a
    ld e,(hl)
    inc hl
    ld d,(hl)
    pop hl
    call l_z80n_muls_32_16x16   ; DEHL
    jr fp_slope
fp_dy1:
    ex de,hl
    ld hl,0
    jr fp_slope
fp_dy2:
    ld a,h
    sra a
    ld d,a
    ld a,l
    rra
    ld e,a
    ld hl,0
    rr h
fp_slope:                       ; 16.8: DE.H
    ld b,h                      ; B = the slope's fraction, DE its whole part
    ld ix,(fp_buf)
    ld a,(fp_vy)
    ld l,a
    ld h,0
    push de
    ex de,hl
    add ix,de                   ; IX = &buf[v.y]
    pop de
    ld a,(fp_dy)
    exx
    ld b,a                      ; B' = rows
    exx
    ld a,(fp_vx)
    ld l,a
    ld h,0
    ld c,0x80                   ; x = v.x + 0.5 (HL.C)
fp_step:
    ld (ix+0),l                 ; buf[y] = x
    inc ix
    ld a,c
    add a,b
    ld c,a
    adc hl,de
    exx
    dec b
    exx
    jr nz,fp_step
fp_next:
    pop bc
    ld a,(fp_i)
    ld hl,fp_y
    add hl,a
    ld a,(fp_ymax)
    cp (hl)
    ret z                       ; w is on the bottom row
    dec b
    jp nz,fp_edge
    ret
fp_end:
    pop bc
    ret

SECTION bss_user
xf_dx: defs 2
xf_dy: defs 2
xf_dz: defs 2
xf_rz: defs 2
xf_o:  defs 2
xf_p:  defs 4
tp_ex: defs 1
tp_ez: defs 1
tp_oe: defs 1
tp_oz: defs 1
tp_hy: defs 2
tp_hz: defs 2
q_args: defs 7
q_ptr:  defs 8
q_any:  defs 1
q_sum:  defs 2
pr_s:   defs 2
fp_x:    defs 4
fp_y:    defs 4
fp_ymin: defs 1
fp_ymax: defs 1
fp_itop: defs 1
fp_buf:  defs 2
fp_dir:  defs 1
fp_i:    defs 1
fp_dy:   defs 1
fp_vx:   defs 1
fp_vy:   defs 1
fp_y0:   defs 1
