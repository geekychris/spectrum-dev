; draw.asm - the crumbs and walls of render.c in Z80N assembly: the draw
; list (depth buckets of 1/4 unit), then far to near onto the Layer 2 back
; buffer. render.c has the same in C (the reference, for the host).
;
; Items (9 bytes): +0 next, +2 colour (C_CRUMB: a crumb, else a wall's side
; shade), +3 offset x (wall) or size (crumb), +4 offset y, +5 corner A's
; record (wall) or left x (crumb), +7 corner B's record or top y.
;
; Polygons wholly on the screen are filled here: each edge is stepped down
; its rows in 8.8 fixed point into a row buffer (down edges into A, up edges
; into B), then every row is a span between A and B, stored by an unrolled
; run of LD (DE),A / INC E. Anything that reaches past the screen's edges
; goes to l2poly, which clips.

SECTION BANK_18

PUBLIC _rd_init
PUBLIC _rd_lists_reset, _rd_crumbs, _rd_walls, _rd_draw, _rd_bucket, _rd_quad
EXTERN _crumb, _rd_cell_corner, _rd_wall, _rd_nwalls, _ed_rec
EXTERN _xf_thr_wall, _xf_thr_crumb, _rd_tsize, _l2_back
EXTERN _rd_stats_walls, _rd_stats_crumbs
EXTERN row_x, row_y, row_z
EXTERN _l2p_poly, _l2p_begin, _l2p_end, _l2p_recip, _l2p_slope_div, _l2p_x_at
EXTERN l_z80n_muls_32_16x16

C_NEST equ 6
C_CRUMB equ 7
C_WALL_TOP equ 8
MAX_ITEMS equ 360
JT_LO equ 0x3A00                ; the last 1.5K of bank 18 (the Makefile keeps it free):
JT_HI equ 0x3B00                ; the span jump table, low and high bytes
BUF_A equ 0x3C00                ; the rows' x from the down edges
BUF_B equ 0x3D00                ; ... the up edges
FLAG_A equ 0x3E00               ; 1: left of the screen, 2: right (FLAG_B 0x3F00)

; ---------------------------------------------------------------- the list

_rd_lists_reset:
    ld hl,_rd_bucket
    ld de,_rd_bucket + 1
    ld bc,511
    ld (hl),0
    ldir
    ld hl,items
    ld (it_ptr),hl
    ld hl,MAX_ITEMS
    ld (it_left),hl
    ld hl,0
    ld (_rd_stats_walls),hl
    ld (_rd_stats_crumbs),hl
    ret

; HL = z (Q8, > 0): IX = a new item at the head of bucket z >> 6 (255 at most);
; carry if the list is full
new_item:
    ld de,(it_left)
    ld a,d
    or e
    scf
    ret z
    dec de
    ld (it_left),de
    ld a,h
    cp 0x40
    ld a,255
    jr nc,ni_b
    add hl,hl
    add hl,hl
    ld a,h
ni_b:
    ld l,a
    ld h,0
    add hl,hl
    ld de,_rd_bucket
    add hl,de                   ; &bucket[b]
    ld ix,(it_ptr)
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (ix+0),e                 ; next = the old head
    ld (ix+1),d
    ld de,(it_ptr)
    ld (hl),d                   ; head = this
    dec hl
    ld (hl),e
    ex de,hl
    ld de,9
    add hl,de
    ld (it_ptr),hl
    or a
    ret

; ---------------------------------------------------------------- crumbs

; void rd_crumbs(void): every crumb between two shown corners (across its
; cell) on this side of the horizon (page 39, the cells' corners, in slot 3)
_rd_crumbs:
    push ix
    push iy
    ld hl,_crumb
    ld (cr_flag),hl
    ld hl,_rd_cell_corner
    ld (cr_cc),hl
    ld bc,486
cr_loop:
    push bc
    ld hl,(cr_flag)
    ld a,(hl)
    inc hl
    ld (cr_flag),hl
    or a
    jp z,cr_next
    ld hl,(cr_cc)               ; both corners shown? (before any sums)
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld a,12
    add a,e
    ld e,a
    ld a,(de)
    or a
    jp z,cr_next
    inc hl
    inc hl
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld a,12
    add a,e
    ld e,a
    ld a,(de)
    or a
    jp z,cr_next
    ld hl,0
    ld (s_dot),hl
    ld (s_z),hl
    ld (s_bx),hl
    ld (s_by),hl
    ld (s_tx),hl
    ld (s_ty),hl
    ld hl,(cr_cc)
    ld b,2                      ; two corners across the cell (0 and 2)
cr_corner:
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    inc hl
    inc hl                      ; (skip the next corner)
    push hl
    push de
    pop iy
    ld a,(iy+12)
    or a
    jp z,cr_hidden
    ld l,(iy+10)                ; s_dot += dot >> 1
    ld h,(iy+11)
    sra h
    rr l
    ld de,(s_dot)
    add hl,de
    ld (s_dot),hl
    ld l,(iy+8)                 ; s_z += z >> 1
    ld h,(iy+9)
    srl h
    rr l
    ld de,(s_z)
    add hl,de
    ld (s_z),hl
    ld l,(iy+0)
    ld h,(iy+1)
    ld de,(s_bx)
    add hl,de
    ld (s_bx),hl
    ld l,(iy+2)
    ld h,(iy+3)
    ld de,(s_by)
    add hl,de
    ld (s_by),hl
    ld l,(iy+4)
    ld h,(iy+5)
    ld de,(s_tx)
    add hl,de
    ld (s_tx),hl
    ld l,(iy+6)
    ld h,(iy+7)
    ld de,(s_ty)
    add hl,de
    ld (s_ty),hl
    pop hl
    djnz cr_corner
    ; on this side of the horizon: the corners' mean dot (all positive here)
    ld hl,(s_dot)
    ld de,(_xf_thr_crumb)
    or a
    sbc hl,de
    jp c,cr_next
    ; 3/4 of the way up: x16 = 2 (sum b + 3 sum t) over the two corners (render.c)
    ld hl,(s_tx)
    ld d,h
    ld e,l
    add hl,hl
    add hl,de
    ld de,(s_bx)
    add hl,de
    add hl,hl
    ld (s_bx),hl                ; x16
    ld hl,(s_ty)
    ld d,h
    ld e,l
    add hl,hl
    add hl,de
    ld de,(s_by)
    add hl,de
    add hl,hl
    ld (s_by),hl                ; y16
    ; radius in 1/16 px
    ld hl,(s_z)
    call tsize
    cp 8
    jr nc,cr_r
    ld a,8
cr_r:
    ld (cr_r16),a
    ; left = (x16 - r) >> 4, top = (y16 - r) >> 4; off screen: skip
    ld e,a
    ld d,0
    ld hl,(s_bx)
    or a
    sbc hl,de
    call sra4
    ld (s_bx),hl
    ld de,8
    add hl,de
    ld a,h
    or a
    jp nz,cr_next               ; left < -8 or left > 247
    ld a,(cr_r16)
    ld e,a
    ld d,0
    ld hl,(s_by)
    or a
    sbc hl,de
    call sra4
    ld (s_by),hl
    ld de,8
    add hl,de
    ld a,h
    or a
    jp nz,cr_next
    ld a,l
    cp 200
    jp nc,cr_next               ; top > 191
    ld hl,(s_z)
    call new_item
    jp c,cr_next
    ld (ix+2),C_CRUMB
    ld a,(cr_r16)               ; size = (2 r + 8) >> 4 = (r + 4) >> 3, 1..8
    cp 60
    ld a,8
    jr nc,cr_s2
    ld a,(cr_r16)
    add a,4
    rra
    rra
    rra
    and 0x1F
cr_s2:
    ld (ix+3),a
    ld hl,(s_bx)
    ld (ix+5),l
    ld (ix+6),h
    ld hl,(s_by)
    ld (ix+7),l
    ld (ix+8),h
    ld hl,(_rd_stats_crumbs)
    inc hl
    ld (_rd_stats_crumbs),hl
    jr cr_next
cr_hidden:
    pop hl
cr_next:
    ld hl,(cr_cc)
    ld de,8
    add hl,de
    ld (cr_cc),hl
    pop bc
    dec bc
    ld a,b
    or c
    jp nz,cr_loop
    pop iy
    pop ix
    ret

; HL >>= 4 (arithmetic)
sra4:
    sra h
    rr l
    sra h
    rr l
    sra h
    rr l
    sra h
    rr l
    ret

; HL = z (Q8) -> A = rd_tsize[min(z >> 7, 127)] (0.13 at that depth, 1/16 px)
tsize:
    add hl,hl
    ld a,h
    jr nc,ts_in
    ld a,127
ts_in:
    cp 128
    jr c,ts_ok
    ld a,127
ts_ok:
    ld l,a
    ld h,0
    ld de,_rd_tsize
    add hl,de
    ld a,(hl)
    ret

; ---------------------------------------------------------------- walls

; void rd_walls(void): every wall of the maze between two shown corners
; (page 40, the edges, in slot 3)
_rd_walls:
    push ix
    push iy
    ld hl,_rd_wall
    ld (wl_ptr),hl
    ld hl,(_rd_nwalls)
    ld (wl_left),hl
wl_loop:
    ld hl,(wl_left)
    ld a,h
    or l
    jp z,wl_done
    dec hl
    ld (wl_left),hl
    ld hl,(wl_ptr)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (wl_ptr),hl
    ex de,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld de,_ed_rec
    add hl,de                   ; the edge's record
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,e                      ; corner A shown? (records are 16-aligned: +12 doesn't carry)
    add a,12
    ld c,a
    ld b,d
    ld a,(bc)
    or a
    jp z,wl_loop
    ld (w_a),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (w_b),de
    ld (w_side),hl
    ; both corners shown, the mean dot past the horizon
    ld iy,(w_a)
    ld a,(iy+12)
    or a
    jr z,wl_loop
    ld l,(iy+10)
    ld h,(iy+11)
    sra h
    rr l
    ld c,(iy+8)
    ld b,(iy+9)                 ; zA
    ld iy,(w_b)
    ld a,(iy+12)
    or a
    jr z,wl_loop
    ld e,(iy+10)
    ld d,(iy+11)
    sra d
    rr e
    add hl,de
    ld de,(_xf_thr_wall)
    or a
    sbc hl,de
    jp m,wl_loop                ; (dots are positive here: no overflow)
    ; all four points off the same side of the screen: not drawn
    ld iy,(w_a)
    ld a,0x0F
    call outcode2
    ld iy,(w_b)
    call outcode2
    or a
    jp nz,wl_loop
    ld iy,(w_b)
    ; z = zA / 2 + zB / 2
    srl b
    rr c
    ld l,(iy+8)
    ld h,(iy+9)
    srl h
    rr l
    add hl,bc
    ld (w_z),hl
    ; the side normal through the camera (the corner rows)
    ld hl,(w_side)
    ld a,(hl)
    xor 0x80
    ld b,a
    inc hl
    ld a,(hl)
    xor 0x80
    ld c,a
    inc hl
    ld a,(hl)
    xor 0x80
    ld ixl,a
    push bc
    call row_x
    ld (w_qx),hl
    pop bc
    push bc
    call row_y
    ld (w_qy),hl
    pop bc
    call row_z
    ld (w_qz),hl
    ld hl,(w_z)
    call new_item
    jp c,wl_done
    ; the thickness: o16 = q * t >> 14 (x), -(q * t) >> 14 (y)
    ld hl,(w_z)
    call tsize
    ld (w_t),a
    ld hl,(w_qx)
    call qmul_t
    ld (w_ox),hl
    ld a,(w_t)
    ld hl,(w_qy)
    call qmul_t
    call neg_hl
    ld (w_oy),hl
    ; toward the ground points: face = ox (bA - tA + bB - tB).x + oy (...).y
    ld iy,(w_a)
    ld l,(iy+0)
    ld h,(iy+1)
    ld e,(iy+4)
    ld d,(iy+5)
    or a
    sbc hl,de
    push hl
    ld l,(iy+2)
    ld h,(iy+3)
    ld e,(iy+6)
    ld d,(iy+7)
    or a
    sbc hl,de
    push hl
    ld iy,(w_b)
    ld l,(iy+0)
    ld h,(iy+1)
    ld e,(iy+4)
    ld d,(iy+5)
    or a
    sbc hl,de
    ex de,hl
    pop bc                      ; dy of A
    pop hl                      ; dx of A
    add hl,de                   ; dx
    push bc
    push hl
    ld l,(iy+2)
    ld h,(iy+3)
    ld e,(iy+6)
    ld d,(iy+7)
    or a
    sbc hl,de
    pop de                      ; dx
    pop bc
    add hl,bc                   ; dy
    ; signs only: 8-bit clamps, two signed 8 x 8 products
    call clamp8
    ld c,a                      ; dy8
    ex de,hl
    call clamp8
    ld b,a                      ; dx8
    ld hl,(w_ox)
    call clamp8
    ld e,b
    call smul8                  ; HL = ox8 dx8
    push hl
    ld hl,(w_oy)
    call clamp8
    ld e,c
    call smul8                  ; oy8 dy8
    pop de
    add hl,de
    bit 7,h
    jr z,wl_facing
    ld hl,(w_ox)
    call neg_hl
    ld (w_ox),hl
    ld hl,(w_oy)
    call neg_hl
    ld (w_oy),hl
wl_facing:
    ld hl,(w_ox)                ; (o16 + 8) >> 4
    ld de,8
    add hl,de
    call sra4
    ld (ix+3),l
    ld hl,(w_oy)
    ld de,8
    add hl,de
    call sra4
    ld (ix+4),l
    ; shade: |qz| >> 11, 0..7
    ld hl,(w_qz)
    bit 7,h
    call nz,neg_hl
    ld a,h
    rra
    rra
    rra
    and 0x1F
    cp 8
    jr c,wl_shade
    ld a,7
wl_shade:
    add a,C_WALL_TOP + 1
    ld (ix+2),a
    ld hl,(w_a)
    ld (ix+5),l
    ld (ix+6),h
    ld hl,(w_b)
    ld (ix+7),l
    ld (ix+8),h
    ld hl,(_rd_stats_walls)
    inc hl
    ld (_rd_stats_walls),hl
    jp wl_loop
wl_done:
    pop iy
    pop ix
    ret

;  A &= the outcodes of corner IY's ground and top points (bit 0: x < -4,
; 1: x > 259, 2: y < -4, 3: y > 195)
outcode2:
    ld l,(iy+0)
    ld h,(iy+1)
    ld e,(iy+2)
    ld d,(iy+3)
    call outcode
    ld l,(iy+4)
    ld h,(iy+5)
    ld e,(iy+6)
    ld d,(iy+7)
outcode:
    ld c,a
    xor a
    push de
    ld de,4
    add hl,de                   ; x + 4: < 0 left, > 263 right
    pop de
    bit 7,h
    jr z,oc_1
    or 1
    jr oc_2
oc_1:
    push de
    ld de,-264
    add hl,de                   ; x + 4 - 264 >= 0: right of it
    pop de
    jr nc,oc_2
    or 2
oc_2:
    ex de,hl
    ld de,4
    add hl,de                   ; y + 4: < 0 above, >= 200 below
    bit 7,h
    jr z,oc_3
    or 4
    jr oc_4
oc_3:
    ld de,-200
    add hl,de
    jr nc,oc_4
    or 8
oc_4:
    and c
    ret

; HL = q (signed), A = t: HL = (q t) >> 14
qmul_t:
    ld c,a
    bit 7,h
    jr z,qm_pos
    call neg_hl                 ; (uses A: t is in C)
    call qm_pos
    jp neg_hl
qm_pos:
    ld d,l
    ld e,c
    mul d,e                     ; lo t
    ld l,d                      ; its high byte
    ld d,h
    ld e,c
    mul d,e                     ; hi t
    ld a,l
    add a,e
    ld e,a
    jr nc,qm_1
    inc d
qm_1:                           ; DE = (q t) >> 8
    ex de,hl
    ld b,6
qm_2:
    srl h
    rr l
    djnz qm_2
    ret

neg_hl:
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ret

; A = HL clamped to -127..127
clamp8:
    ld a,h
    or a
    jr nz,c8_big
    ld a,l
    cp 128
    ret c
    ld a,127
    ret
c8_big:
    inc a
    jr nz,c8_neg
    ld a,l                      ; H = 0xFF
    cp 0x81
    ret nc
c8_neg:
    bit 7,h
    ld a,127
    ret z
    ld a,-127
    ret

; HL = A * E (signed 8 x 8)
smul8:
    ld d,a
    xor e
    push af                     ; the sign of the product
    ld a,d
    bit 7,a
    jr z,sm_1
    neg
sm_1:
    ld d,a
    ld a,e
    bit 7,a
    jr z,sm_2
    neg
sm_2:
    ld e,a
    mul d,e
    ex de,hl
    pop af
    ret p
    jp neg_hl

; ---------------------------------------------------------------- drawing

; void rd_draw(void): the buckets far to near (l2p_begin has been called)
_rd_draw:
    push ix
    push iy
    ld a,(_l2_back)
    add a,a
    ld (l2base),a
    ld a,0xFF
    ld (l2page),a
    ld hl,_rd_bucket + 510
dw_bucket:
    ld e,(hl)
    inc hl
    ld d,(hl)
    dec hl
    push hl
    ex de,hl
dw_item:
    ld a,h
    or l
    jr z,dw_bucket_done
    push hl
    pop ix
    ld a,(ix+2)
    cp C_CRUMB
    jr nz,dw_wall
    call draw_crumb
    jr dw_item_next
dw_wall:
    call draw_wall
dw_item_next:
    ld l,(ix+0)
    ld h,(ix+1)
    jr dw_item
dw_bucket_done:
    pop hl
    ld de,_rd_bucket
    or a
    sbc hl,de
    jr z,dw_done
    add hl,de
    dec hl
    dec hl
    jr dw_bucket
dw_done:
    pop iy
    pop ix
    ret

; IX = a crumb: a size x size square at (left, top), clipped
draw_crumb:
    ld a,(ix+2)
    ld (pcol),a
    ld b,(ix+3)                 ; size
    ld e,(ix+7)
    ld d,(ix+8)                 ; top
dc_row:
    ld a,d
    or a
    jr nz,dc_skip               ; above (or far below) the screen
    ld a,e
    cp 192
    jr nc,dc_skip
    push bc
    push de
    call map_row                ; D = the row's address high byte
    ld b,(ix+3)                 ; pixels (B was the rows left)
    ld c,(ix+5)
    ld a,(ix+6)                 ; left
    ld e,c
    or a
    jr z,dc_in
    ; left < 0: start at 0, fewer pixels
    ld a,c
    add a,b
    jr nc,dc_out                ; all of it left of 0
    jr z,dc_out
    ld b,a
    ld e,0
dc_in:
    ld a,e                      ; right edge past 255?
    add a,b
    jr nc,dc_fill
    jr z,dc_fill
    ld a,e
    neg
    ld b,a
dc_fill:
    ld a,(pcol)
dc_px:
    ld (de),a
    inc e
    djnz dc_px
dc_out:
    pop de
    pop bc
dc_skip:
    inc de
    djnz dc_row
    ret

; IX = a wall: the side (A top, B top, B ground, A ground, all + o) and the
; top (A top + o, B top + o, B top - o, A top - o)
draw_wall:
    ld a,(ix+3)
    call sext_a
    ld (pox),hl
    ld a,(ix+4)
    call sext_a
    ld (poy),hl
    ld e,(ix+5)
    ld d,(ix+6)
    push de
    pop iy                      ; A
    ld hl,qv
    ld b,4
    call vtx_plus               ; A top + o
    ld e,(ix+7)
    ld d,(ix+8)
    push de
    pop iy                      ; B
    ld b,4
    call vtx_plus               ; B top + o
    ld b,0
    call vtx_plus               ; B ground + o
    ld e,(ix+5)
    ld d,(ix+6)
    push de
    pop iy
    ld b,0
    call vtx_plus               ; A ground + o
    ld a,(ix+2)
    ld (pcol),a
    xor a
    ld (pfast),a
    ld (pskip0),a
    call poly4
    ld a,(pfast)                ; the top shares the side's first edge (the
    ld (pskip0),a               ; same rows, the same buffer) if both are fast
    ; the top: A + o, B + o stay; B top - o, A top - o
    ld e,(ix+7)
    ld d,(ix+8)
    push de
    pop iy
    ld hl,qv + 8
    ld b,4
    call vtx_minus
    ld e,(ix+5)
    ld d,(ix+6)
    push de
    pop iy
    ld b,4
    call vtx_minus
    ld a,C_WALL_TOP
    ld (pcol),a
    call poly4
    xor a
    ld (pskip0),a
    ret

; (HL) = corner IY's point at offset B (0 ground, 4 top) + (pox, poy); HL += 4
vtx_plus:
    push hl
    ld a,b
    ld (vp_off + 2),a           ; patch the IY offsets
    add a,1
    ld (vp_off + 5),a
    add a,1
    ld (vp_off2 + 2),a
    add a,1
    ld (vp_off2 + 5),a
vp_off:
    ld e,(iy+0)
    ld d,(iy+1)
    ld hl,(pox)
    add hl,de
    ex de,hl
    pop hl
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    push hl
vp_off2:
    ld e,(iy+2)
    ld d,(iy+3)
    ld hl,(poy)
    add hl,de
    ex de,hl
    pop hl
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ret

; the same, minus (pox, poy)
vtx_minus:
    push hl
    ld a,b
    ld (vm_off + 2),a
    add a,1
    ld (vm_off + 5),a
    add a,1
    ld (vm_off2 + 2),a
    add a,1
    ld (vm_off2 + 5),a
vm_off:
    ld l,(iy+0)
    ld h,(iy+1)
    ld de,(pox)
    or a
    sbc hl,de
    ex de,hl
    pop hl
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    push hl
vm_off2:
    ld l,(iy+2)
    ld h,(iy+3)
    ld de,(poy)
    or a
    sbc hl,de
    ex de,hl
    pop hl
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ret

sext_a:
    ld l,a
    rlca
    sbc a,a
    ld h,a
    ret

; ---------------------------------------------------------------- the quad fill

; void rd_quad(const int16_t *xy, uint8_t colour): one quad (tests; l2p_begin first)
_rd_quad:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    ld (pcol),a
    ex de,hl
    ld de,qv
    ld bc,16
    ldir
    ld a,(_l2_back)
    add a,a
    ld (l2base),a
    ld a,0xFF
    ld (l2page),a
    push ix
    push iy
    call poly4
    pop iy
    pop ix
    ret

; the quad in qv (x0, y0 ... x3, y3, int16) in colour pcol, clipped to the
; screen: rows outside 0..191 are skipped as the edges are stepped; if every
; x is on the screen, x is stepped in 8.8 into byte buffers (BUF_A / BUF_B),
; else in 16.8, clamped to 0..255 with a flag (1 left of the screen, 2
; right of it) in FLAG_A / FLAG_B, and a row whose two ends are off the same
; side is skipped
poly4:
    ; wholly on the screen (every x 0..255, every y 0..191): the 8-bit path
    ld hl,qv
    ld b,4
    ld c,192                    ; ymin
    ld d,0                      ; ymax
pf_chk:
    inc hl
    ld a,(hl)                   ; x high
    or a
    jr nz,p4_general
    inc hl
    ld a,(hl)                   ; y low
    inc hl
    ld e,a
    ld a,(hl)                   ; y high
    inc hl
    or a
    jr nz,p4_general
    ld a,e
    cp 192
    jr nc,p4_general
    cp c
    jr nc,pf_1
    ld c,a
pf_1:
    cp d
    jr c,pf_2
    ld d,a
pf_2:
    djnz pf_chk
    ld a,c
    cp d
    ret z                       ; no rows
    ld (pymin),a
    ld a,d
    ld (pymax),a
    xor a
    ld (pclip),a
    ld a,(pskip0)               ; the top of a wall: its first edge is the side's
    or a
    jr nz,pf_e1
    ld hl,qv
    ld de,qv + 4
    call edge8
pf_e1:
    ld hl,qv + 4
    ld de,qv + 8
    call edge8
    ld hl,qv + 8
    ld de,qv + 12
    call edge8
    ld hl,qv + 12
    ld de,qv
    call edge8
    ld a,1
    ld (pfast),a
    jp p4_rows

p4_general:
    xor a
    ld (pfast),a
    ld hl,qv
    call ld_de_hl
    ld (pxmin),de
    ld (pxmax),de
    call ld_de_hl
    ld (pymn),de
    ld (pymx),de
    ld b,3
p4_box:
    push bc
    call ld_de_hl               ; x
    push hl
    ld hl,(pxmin)
    call cmp_de_hl              ; carry: DE < HL
    jr nc,p4_b1
    ld (pxmin),de
p4_b1:
    ld hl,(pxmax)
    ex de,hl
    call cmp_de_hl              ; carry: HL(old max) < DE(x)?  (args swapped)
    jr nc,p4_b2
    ld (pxmax),hl
p4_b2:
    pop hl
    call ld_de_hl               ; y
    push hl
    ld hl,(pymn)
    call cmp_de_hl
    jr nc,p4_b3
    ld (pymn),de
p4_b3:
    ld hl,(pymx)
    ex de,hl
    call cmp_de_hl
    jr nc,p4_b4
    ld (pymx),hl
p4_b4:
    pop hl
    pop bc
    djnz p4_box
    ; off the screen altogether?
    ld hl,(pymx)
    ld a,h
    or a
    ret m                       ; ymax < 0
    or l
    ret z                       ; ymax = 0
    ld hl,(pymn)
    ld de,192
    call cmp_hl_de_s
    ret nc                      ; ymin >= 192
    ld hl,(pxmax)
    bit 7,h
    ret nz                      ; xmax < 0
    ld hl,(pxmin)
    ld a,h
    or a
    jr z,p4_x1
    ret p                       ; xmin > 255
p4_x1:
    ; the rows: ys = max(ymin, 0), ye = min(ymax, 192)
    ld hl,(pymn)
    bit 7,h
    jr z,p4_y1
    ld hl,0
p4_y1:
    ld a,l
    ld (pymin),a
    ld hl,(pymx)
    ld de,192
    call cmp_hl_de_s
    jr c,p4_y2
    ex de,hl
p4_y2:
    ld a,l
    ld (pymax),a
    ld a,(pymin)
    cp l
    ret nc                      ; no rows
    ; every x on the screen?
    xor a
    ld (pclip),a
    ld hl,(pxmin)
    ld a,h
    or a
    jr nz,p4_clip
    ld hl,(pxmax)
    ld a,h
    or a
    jr z,p4_edges
p4_clip:
    ld a,1
    ld (pclip),a
p4_edges:
    ld hl,qv
    ld de,qv + 4
    call edge
    ld hl,qv + 4
    ld de,qv + 8
    call edge
    ld hl,qv + 8
    ld de,qv + 12
    call edge
    ld hl,qv + 12
    ld de,qv
    call edge
p4_rows:
    ; B = rows, C = the row (the buffers' index), D = its address high byte,
    ; IXH = the colour; spans go into the unrolled stores through JT
    push ix
    ld a,(pcol)
    ld ixh,a
    ld a,(pymin)
    ld c,a
    ld e,a
    call map_row                ; D = 0x60 + row & 31 (the page mapped)
    ld a,(pymax)
    sub c
    ld b,a
    ld a,(pclip)
    or a
    jr nz,p4_rows_clip
p4_row:
    ld l,c
    ld h,BUF_A >> 8
    ld e,(hl)
    inc h
    ld a,(hl)                   ; E = A's x, A = B's x
    cp e
    jr nc,p4_ord
    ld h,a
    ld a,e
    ld e,h                      ; left = E, right = A
p4_ord:
    sub e                       ; pixels - 1
    ld l,a
    ld h,JT_LO >> 8
    ld a,(hl)
    inc h
    ld h,(hl)
    ld l,a                      ; the entry for that many
    ld a,ixh
    call jp_hl
p4_next:
    inc c
    inc d
    ld a,d
    cp 0x80
    call z,next_page
    djnz p4_row
    pop ix
    ret

p4_rows_clip:                   ; the same, skipping rows with both ends off one side
    ld l,c
    ld h,FLAG_A >> 8
    ld a,(hl)
    inc h
    and (hl)
    jr nz,pc_next
    ld h,BUF_A >> 8
    ld e,(hl)
    inc h
    ld a,(hl)
    cp e
    jr nc,pc_ord
    ld h,a
    ld a,e
    ld e,h
pc_ord:
    sub e
    ld l,a
    ld h,JT_LO >> 8
    ld a,(hl)
    inc h
    ld h,(hl)
    ld l,a
    ld a,ixh
    call jp_hl
pc_next:
    inc c
    inc d
    ld a,d
    cp 0x80
    call z,next_page
    djnz p4_rows_clip
    pop ix
    ret

jp_hl:
    jp (hl)

; the row's address passed the page: the next 8K page, D = 0x60 (keeps BC, E)
next_page:
    ld a,(l2page)
    inc a
    ld (l2page),a
    nextreg 0x53,a
    ld d,0x60
    ret

; the edge from (HL) to (DE), both on the screen, into the row buffers
edge8:
    push de
    ld c,(hl)                   ; x0
    inc hl
    inc hl
    ld b,(hl)                   ; y0
    pop hl
    ld e,(hl)                   ; x1
    inc hl
    inc hl
    ld d,(hl)                   ; y1
    ld a,d
    sub b
    ret z                       ; flat
    jr nc,e8_down
    neg                         ; up: from (x1, y1) down to (x0, y0) into B
    ld h,BUF_B >> 8
    ld l,d
    ld b,a                      ; rows
    ld a,c
    sub e                       ; dx = x0 - x1
    ld c,e                      ; x top
    jr e8_walk
e8_down:
    ld h,BUF_A >> 8
    ld l,b
    ld b,a
    ld a,e
    sub c                       ; dx = x1 - x0, x top = C
e8_walk:
    ; A = dx (carry: its sign), B = rows, C = x top, HL = the buffer row
    ld e,a
    sbc a,a
    ld d,a                      ; DE = dx
    ld a,b
    cp 1
    jr nz,e8_slope
    ld (hl),c                   ; one row
    ret
e8_slope:
    ; slope (8.8) = |dx| * (65536 / rows) >> 8, signed
    push hl
    push bc
    ld l,a
    ld h,0
    add hl,hl
    ld bc,_l2p_recip
    add hl,bc
    ld c,(hl)
    inc hl
    ld h,(hl)
    ld l,c                      ; HL = 65536 / rows
    cp 2
    jr nz,e8_r
    ld hl,32768                 ; (no table entry for 2)
e8_r:
    bit 7,d
    push af                     ; the sign of dx
    jr z,e8_pos
    xor a
    sub e
    ld e,a                      ; |dx| (>= -255)
e8_pos:
    ld a,e
    ld d,l
    ld e,a
    mul d,e                     ; |dx| recip_lo
    ld b,d
    ld d,h
    ld e,a
    mul d,e                     ; |dx| recip_hi
    ld a,b
    add a,e
    ld e,a
    jr nc,e8_s1
    inc d
e8_s1:                          ; DE = |slope| (8.8)
    pop af
    jr z,e8_s2
    xor a
    sub e
    ld e,a
    sbc a,a
    sub d
    ld d,a
e8_s2:
    pop bc                      ; B = rows, C = x top
    pop hl                      ; the buffer row
    push ix
    ld ixl,b
    ld b,d
    ld a,c
    ld c,e                      ; BC = slope
    ex de,hl                    ; DE = the buffer row
    ld h,a
    ld l,0x80                   ; HL = x top + 0.5
e8_loop:
    ld a,h
    ld (de),a
    inc e
    add hl,bc
    dec ixl
    jr nz,e8_loop
    pop ix
    ret

; DE = (HL), HL += 2
ld_de_hl:
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ret

; carry if DE < HL (signed); keeps DE, HL
cmp_de_hl:
    push hl
    push de
    ex de,hl
    call cmp_hl_de_s
    pop de
    pop hl
    ret

; carry if HL < DE (signed); keeps DE, HL
cmp_hl_de_s:
    push hl
    ld a,h
    xor d
    jp m,chd_diff
    or a
    sbc hl,de
    pop hl
    ret
chd_diff:
    pop hl
    bit 7,h                     ; signs differ: HL < DE if HL is the negative one
    scf
    ret nz
    or a
    ret

; the edge from (HL) to (DE) (x, y int16) into the row buffers, its rows
; clipped to 0..191
edge:
    ld (e_p0),hl
    ld (e_p1),de
    inc hl
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = y0
    ex de,hl
    inc hl
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = y1
    ex de,hl                    ; HL = y1
    or a
    sbc hl,bc                   ; y1 - y0
    ret z                       ; flat
    jp m,ed_up
    ld a,BUF_A >> 8             ; down: from p0
    ld (e_buf),a
    ld hl,(e_p0)
    ld de,(e_p1)
    jr ed_dir
ed_up:
    ld a,BUF_B >> 8             ; up: from p1, down to p0
    ld (e_buf),a
    ld hl,(e_p1)
    ld de,(e_p0)
ed_dir:
    ; HL -> the top point, DE -> the bottom one
    push de
    call ld_de_hl
    ld (e_xt),de
    call ld_de_hl
    ld (e_yt),de
    pop hl
    call ld_de_hl
    ld (e_xb),de
    call ld_de_hl               ; DE = y bottom
    ; rows: ys = max(yt, 0), ye = min(yb, 192), none if ys >= ye
    ld hl,192
    ex de,hl
    call cmp_hl_de_s            ; yb < 192 ?
    jr c,ed_ye
    ex de,hl
ed_ye:
    ld (e_ye),hl                ; ye (may be <= 0)
    ld hl,(e_yt)
    bit 7,h
    jr z,ed_ys
    ld hl,0
ed_ys:
    ld (e_ys),hl
    ld de,(e_ye)
    call cmp_hl_de_s
    ret nc                      ; ys >= ye
    ex de,hl
    or a
    sbc hl,de
    ld a,l
    ld (e_rows),a               ; 1..192
    ; dx, dy
    ld hl,(e_xb)
    ld de,(e_xt)
    or a
    sbc hl,de
    ld (e_dx),hl
    ; the slope (16.8): (dx << 8) / dy, dy = |y1 - y0|
    ld hl,(e_p0)
    inc hl
    inc hl
    call ld_de_hl
    push de
    ld hl,(e_p1)
    inc hl
    inc hl
    call ld_de_hl
    pop hl
    ex de,hl
    or a
    sbc hl,de
    jp p,ed_dy
    call neg_hl
ed_dy:
    ld (e_dy),hl
    ld a,h
    or a
    jr nz,ed_div                ; dy >= 256: divide
    ld a,l
    cp 2
    jr nz,ed_r3
    ld de,32767                 ; (no table entry for 2; the multiply is signed)
    jr ed_r
ed_r3:
    cp 1
    jr nz,ed_rt
    ld de,0                     ; dy = 1: one row, any slope
    jr ed_r
ed_rt:
    add hl,hl
    ld de,_l2p_recip
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = 65536 / dy
ed_r:
    ld hl,(e_dx)
    bit 7,h
    push af
    call nz,neg_hl              ; |dx|
    call l_z80n_muls_32_16x16   ; DEHL = |dx| 65536 / dy (positive)
    ld a,h                      ; 16.8: int = DE, frac = H
    pop af
    jr z,ed_sl
    ; negate the 24-bit D:E:A
    ld l,a
    xor a
    sub l
    ld l,a
    ld a,0
    sbc a,e
    ld e,a
    ld a,0
    sbc a,d
    ld d,a
    ld a,l
ed_sl:
    ld (e_sf),a
    ld (e_si),de
    jr ed_start
ed_div:
    ld hl,(e_dy)
    push hl
    ld hl,(e_dx)
    push hl
    call _l2p_slope_div         ; DEHL = (dx << 16) / dy
    pop af
    pop af
    ld a,h
    ld (e_sf),a
    ld (e_si),de
ed_start:
    ; x at row ys: xt + 0.5 + slope (ys - yt)
    ld hl,(e_ys)
    ld de,(e_yt)
    or a
    sbc hl,de
    jr nz,ed_skip
    ld hl,(e_xt)
    ld a,0x80
    jr ed_x
ed_skip:
    push hl                     ; t
    ld hl,(e_dy)
    push hl
    ld hl,(e_dx)
    push hl
    ld hl,(e_xt)
    push hl
    call _l2p_x_at              ; DEHL = x (16.16, + 0.5)
    pop af
    pop af
    pop af
    pop af
    ld a,h                      ; frac
    ex de,hl                    ; HL = int
ed_x:
    ; HL = x int, A = frac; rows from ys, e_rows of them
    ld c,a
    ld a,(pclip)
    or a
    jr nz,ed_clip
    ; on the screen: 8.8 in HL (H = int, L = frac), BC = slope 8.8
    ld h,l
    ld l,c
    ld a,(e_si)
    ld b,a
    ld a,(e_sf)
    ld c,a
    ld a,(e_ys)
    ld e,a
    ld a,(e_buf)
    ld d,a
    ld a,(e_rows)
    push ix
    ld ixl,a
ed_loop:
    ld a,h
    ld (de),a
    inc e
    add hl,bc
    dec ixl
    jr nz,ed_loop
    pop ix
    ret
ed_clip:
    ; 16.8 in HL (int) and C (frac), DE = slope int, B = slope frac;
    ; x and the flag stored through e_ptr
    ld a,(e_ys)
    ld (e_ptr),a
    ld a,(e_buf)
    ld (e_ptr + 1),a
    ld de,(e_si)
    ld a,(e_sf)
    ld b,a
ec_loop:
    push hl
    push de
    ld a,h
    or a
    jr nz,ec_out
    ld e,l
    ld d,0                      ; x, flag 0
    jr ec_put
ec_out:
    ld e,0
    ld d,1                      ; left of the screen
    bit 7,h
    jr nz,ec_put
    ld e,255
    ld d,2                      ; right of it
ec_put:
    ld hl,(e_ptr)
    ld (hl),e
    inc h
    inc h
    ld (hl),d                   ; the flag: two pages on
    dec h
    dec h
    inc l
    ld (e_ptr),hl
    pop de
    pop hl
    ld a,c                      ; step
    add a,b
    ld c,a
    adc hl,de
    ld a,(e_rows)
    dec a
    ld (e_rows),a
    jr nz,ec_loop
    ret

; the row E's 8K page into slot 3: D = the row's address high byte (0x60 + row & 31)
map_row:
    ld a,e
    rlca
    rlca
    rlca
    and 7
    ld d,a
    ld a,(l2base)
    add a,d
    ld d,a
    ld a,(l2page)
    cp d
    jr z,mr_same
    ld a,d
    ld (l2page),a
    nextreg 0x53,a
mr_same:
    ld a,e
    and 31
    or 0x60
    ld d,a
    ret

; C + 1 pixels from (D, E) in pcol (the crumbs)
span:
    ld l,c
    ld h,JT_LO >> 8
    ld a,(hl)
    inc h
    ld h,(hl)
    ld l,a
    ld a,(pcol)
    jp (hl)

; the unrolled stores: entered 2 n bytes before sp_end to store n pixels
sp_run:
    REPT 256
    ld (de),a
    inc e
    ENDR
sp_end:
    ret

; void rd_init(void): the jump table, JT[n - 1] = sp_end - 2 n (once)
_rd_init:
    ld hl,sp_end - 2
    ld de,JT_LO
    ld b,0
ri_loop:
    ld a,l
    ld (de),a
    inc d
    ld a,h
    ld (de),a
    dec d
    dec hl
    dec hl
    inc e
    djnz ri_loop
    ret

SECTION BANK_18
_rd_bucket: defs 512
items:      defs MAX_ITEMS * 9
it_ptr:     defw 0
it_left:    defw 0
cr_flag:    defw 0
cr_cc:      defw 0
cr_r16:     defb 0
s_dot:      defw 0
s_z:        defw 0
s_bx:       defw 0
s_by:       defw 0
s_tx:       defw 0
s_ty:       defw 0
wl_ptr:     defw 0
wl_left:    defw 0
w_a:        defw 0
w_b:        defw 0
w_side:     defw 0
w_z:        defw 0
w_qx:       defw 0
w_qy:       defw 0
w_qz:       defw 0
w_t:        defb 0
w_ox:       defw 0
w_oy:       defw 0
pox:        defw 0
poy:        defw 0
pcol:       defb 0
pymin:      defb 0
pymax:      defb 0
prow:       defb 0
l2base:     defb 0
l2page:     defb 0
qv:         defs 16
pxmin:      defw 0
pxmax:      defw 0
pymn:       defw 0
pymx:       defw 0
pclip:      defb 0
pfast:      defb 0
pskip0:     defb 0
e_p0:       defw 0
e_p1:       defw 0
e_buf:      defb 0
e_xt:       defw 0
e_yt:       defw 0
e_xb:       defw 0
e_ys:       defw 0
e_ye:       defw 0
e_rows:     defb 0
e_dx:       defw 0
e_dy:       defw 0
e_si:       defw 0
e_sf:       defb 0
e_ptr:      defw 0
