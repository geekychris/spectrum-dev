; Lines, wireframe polygons and point lists on the 48K screen (line48.h).
;
; Bresenham in eight loops, one per octant (x-major or y-major, x and y
; increasing or decreasing). Registers in the loops: HL = screen byte,
; C = pixel mask, D = y (0..255; the row table wraps it mod 192), E = error,
; B = pixels left, IXH = major length, IXL = minor length. A polygon's edges
; continue from where the last one stopped, so only its first vertex needs
; an address. The pixel operation (xor / or / and with an inverted mask for
; CLEAR) is patched into the loops when line_mode changes.
;
; Row tables (line48.c builds them): 0xF200 + y = low byte of the address of
; line y mod 192 (column 0), 0xF300 + y = high byte; y 192..223 are lines
; 0..31 and 224..255 are lines 160..191 (-32..-1). Moving a line down within
; a character cell is INC H; across cells (and the wrap) the table.

SECTION code_user

PUBLIC _line_x0, _line_y0, _line_x1, _line_y1, _line_mode, _line_skip, _line_signed
PUBLIC _line_core, _lp_core, _pl_core
PUBLIC _lp_cx, _lp_cy, _lp_pts, _lp_n, _lp_closed
PUBLIC _pl_ptr, _pl_n
PUBLIC _pd_core, _pd_x, _pd_y, _pd_ptr, _ps_core, _ps_on, _ps_want, _ps_n

defc ROWLO = 0xF2

; ------------------------------------------------------------------ mode

; patch the loops for line_mode if it changed: the operation (A9 xor c,
; B1 or c, A1 and c) and the carry test after a mask rotate (CLEAR keeps the
; mask inverted, so a byte boundary is "no carry": jr c instead of jr nc)
set_mode:
    ld a,(_line_mode)
    ld hl,cur_mode
    cp (hl)
    ret z
    ld (hl),a
    ld c,0xA9
    ld b,0x30
    ld e,0
    or a
    jr z,sm_go
    ld c,0xB1
    dec a
    jr z,sm_go
    ld c,0xA1
    ld b,0x38
    ld e,0xFF
sm_go:
    ld a,e
    ld (mask_inv),a
    ld hl,op_tab
    ld e,9
sm_op:
    push de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,c
    ld (de),a
    pop de
    dec e
    jr nz,sm_op
    ld hl,jx_tab
    ld e,8
sm_jx:
    push de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,b
    ld (de),a
    pop de
    dec e
    jr nz,sm_jx
    ret

bitmask:  defb 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01

; B = x, D = y -> HL = screen byte, C = mask (inverted for CLEAR)
start_at:
    ld a,b
    and 7
    ld hl,bitmask
    add a,l
    ld l,a
    adc a,h
    sub l
    ld h,a
    ld a,(mask_inv)
    xor (hl)
    ld c,a
    ld l,d
    ld h,ROWLO
    ld a,b
    rrca
    rrca
    rrca
    and 0x1F
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
    ret

; ------------------------------------------------------------------ an edge

; from the pixel at HL / C / D: ed_adx, ed_ady (lengths), ed_dir (bit 0 = x
; decreasing, bit 1 = y decreasing), line_skip (leave out the last pixel).
; Leaves HL / C / D on the pixel after the last one drawn.
edge_go:
    ld a,(ed_adx)
    ld e,a
    ld a,(ed_ady)
    cp e
    jr c,eg_x                   ; ady < adx: x major
    jr z,eg_x                   ; equal: x major
    ld ixh,a                    ; y major
    ld ixl,e
    ld e,4
    jr eg_n
eg_x:
    ld ixl,a
    ld a,e
    ld ixh,a
    ld e,0
eg_n:                           ; A = major
    ld b,a
    srl a
    push af                     ; error = major / 2
    inc b                       ; pixels = major + 1 (0 = 256) ...
    ld a,(_line_skip)
    or a
    jr z,eg_ns
    dec b                       ; ... - 1 without the last
    jr z,eg_none
eg_ns:
    ld a,(ed_dir)
    or e
    add a,a
    push hl
    ld hl,loop_tab
    add a,l
    ld l,a
    adc a,h
    sub l
    ld h,a
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld (eg_jp + 1),hl
    pop hl
    pop af
    ld e,a
eg_jp:
    jp 0
eg_none:
    pop af
    ret

; ------------------------------------------------------------------ one line

; line_x0..y1; line_signed: differences as -128..127 (wrapped coordinates),
; else 0..255 with the direction from the comparison
_line_core:
    push ix
    call set_mode
    call line_one
    pop ix
    ret

line_one:
    xor a
    ld (ed_dir),a
    ld a,(_line_x1)
    ld hl,_line_x0
    sub (hl)
    ld e,a
    sbc a,a
    ld d,a                      ; FF: x1 < x0 (unsigned)
    ld a,(_line_signed)
    or a
    jr z,lo_xu
    ld a,e
    add a,a
    sbc a,a
    ld d,a                      ; FF: dx < 0 (signed)
lo_xu:
    ld a,d
    or a
    ld a,e
    jr z,lo_xp
    neg
    ld hl,ed_dir
    set 0,(hl)
lo_xp:
    ld (ed_adx),a
    ld a,(_line_y1)
    ld hl,_line_y0
    sub (hl)
    ld e,a
    sbc a,a
    ld d,a
    ld a,(_line_signed)
    or a
    jr z,lo_yu
    ld a,e
    add a,a
    sbc a,a
    ld d,a
lo_yu:
    ld a,d
    or a
    ld a,e
    jr z,lo_yp
    neg
    ld hl,ed_dir
    set 1,(hl)
lo_yp:
    ld (ed_ady),a
    ld a,(_line_x0)
    ld b,a
    ld a,(_line_y0)
    ld d,a
    call start_at
    jp edge_go

; ------------------------------------------------------------------ polygon

; lp_n vertices (x, y signed bytes) at lp_pts around (lp_cx, lp_cy), wrapped;
; every edge without its last pixel; lp_closed = back to the first vertex,
; else the last vertex is plotted
_lp_core:
    push ix
    call set_mode
    ld a,1
    ld (_line_skip),a
    ld hl,(_lp_pts)
    ld a,(hl)
    ld (prev_x),a
    ld e,a
    ld a,(_lp_cx)
    add a,e
    ld b,a
    inc hl
    ld a,(hl)
    ld (prev_y),a
    ld e,a
    ld a,(_lp_cy)
    add a,e
    ld d,a
    inc hl
    ld (lp_next),hl
    call start_at
    ld a,(_lp_n)
    dec a
    jr z,lp_last
    ld (lp_left),a
lp_loop:
    call lp_edge
    ld a,(lp_left)
    dec a
    ld (lp_left),a
    jr nz,lp_loop
lp_last:
    ld a,(_lp_closed)
    or a
    jr z,lp_open
    push hl
    ld hl,(_lp_pts)
    ld (lp_next),hl
    pop hl
    call lp_edge
    pop ix
    ret
lp_open:
    ld b,1                      ; the last vertex: one pixel
    call xm_pp
    pop ix
    ret

; the edge from (prev_x, prev_y) to the vertex at lp_next
lp_edge:
    push hl
    ld hl,(lp_next)
    ld a,(hl)
    inc hl
    ld e,(hl)
    inc hl
    ld (lp_next),hl
    ld hl,prev_x
    ld b,(hl)
    ld (hl),a
    sub b                       ; dx (offsets: no overflow)
    ld b,0
    jp p,le_xp
    neg
    inc b
le_xp:
    ld (ed_adx),a
    ld hl,prev_y
    ld a,e
    sub (hl)
    ld (hl),e
    jp p,le_yp
    neg
    set 1,b
le_yp:
    ld (ed_ady),a
    ld a,b
    ld (ed_dir),a
    pop hl
    jp edge_go

; ------------------------------------------------------------------ compiled polygons

; poly_compile (line48.c) output: first vertex x, y, edge count n, then per
; edge: loop index * 2, pixels (0 = none), error, major, minor. Draws like
; line_poly(closed) without working out each edge.
_pd_core:
    push ix
    call set_mode
    call pd_one
    pop ix
    ret

pd_one:
    ld hl,(_pd_ptr)
    ld a,(_pd_x)
    add a,(hl)
    ld b,a
    inc hl
    ld a,(_pd_y)
    add a,(hl)
    ld d,a
    inc hl
    ld a,(hl)
    inc hl
    ld (pd_left),a
    ld (pd_rec),hl
    call start_at
pd_edge:
    push hl
    ld hl,(pd_rec)
    ld a,(hl)                   ; loop index * 2
    ld (pd_idx),a
    inc hl
    ld b,(hl)                   ; pixels
    inc hl
    ld e,(hl)                   ; error
    inc hl
    ld a,(hl)
    ld ixh,a                    ; major
    inc hl
    ld a,(hl)
    ld ixl,a                    ; minor
    inc hl
    ld (pd_rec),hl
    ld a,(pd_idx)
    ld hl,loop_tab
    add a,l
    ld l,a
    adc a,h
    sub l
    ld h,a
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld (pd_jp + 1),hl
    pop hl
    ld a,b
    or a
    jr z,pd_skip
pd_jp:
    call 0
pd_skip:
    ld a,(pd_left)
    dec a
    ld (pd_left),a
    jr nz,pd_edge
    ret

; poly_sync: ps_n entries {x, y, poly} in ps_on / ps_want; each that differs
; is drawn where it was (if any) and where it is wanted, and copied
_ps_core:
    push ix
    call set_mode
    ld a,(_ps_n)
    or a
    jr z,ps_done
    ld (ps_left),a
ps_loop:
    ld hl,(_ps_on)
    ld de,(_ps_want)
    ld b,4
ps_cmp:
    ld a,(de)
    cp (hl)
    jr nz,ps_diff
    inc hl
    inc de
    djnz ps_cmp
    jr ps_next
ps_diff:
    ld hl,(_ps_on)
    call ps_draw
    ld hl,(_ps_want)
    call ps_draw
    ld hl,(_ps_want)
    ld de,(_ps_on)
    ldi
    ldi
    ldi
    ldi
ps_next:
    ld hl,(_ps_on)
    ld de,4
    add hl,de
    ld (_ps_on),hl
    ld hl,(_ps_want)
    add hl,de
    ld (_ps_want),hl
    ld a,(ps_left)
    dec a
    ld (ps_left),a
    jr nz,ps_loop
ps_done:
    pop ix
    ret

; the entry at HL, if it has a polygon
ps_draw:
    ld a,(hl)
    ld (_pd_x),a
    inc hl
    ld a,(hl)
    ld (_pd_y),a
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    or h
    ret z
    ld (_pd_ptr),hl
    jp pd_one

; ------------------------------------------------------------------ points

; pl_n pixels from x,y byte pairs at pl_ptr; y >= 192 skipped
_pl_core:
    call set_mode
    ld a,(_pl_n)
    or a
    ret z
    ld b,a
    ld hl,(_pl_ptr)
pl_loop:
    ld c,(hl)                   ; x
    inc hl
    ld a,(hl)                   ; y
    inc hl
    cp 192
    jr nc,pl_skip
    push hl
    ld l,a
    ld h,ROWLO
    ld a,c
    rrca
    rrca
    rrca
    and 0x1F
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
    ld a,c
    and 7
    ld de,bitmask
    add a,e
    ld e,a
    adc a,d
    sub e
    ld d,a
    ld a,(mask_inv)
    ex de,hl
    xor (hl)
    ex de,hl
    ld c,a
pl_op:
    ld a,(hl)
    xor c
    ld (hl),a
    pop hl
pl_skip:
    djnz pl_loop
    ret

; ------------------------------------------------------------------ loops
; ---- x-major, x +, y +
xm_pp:
xm_pp_op:
    ld a,(hl)
    xor c
    ld (hl),a
xm_pp_jx:
    rrc c
    jr nc,xm_pp_e
    ld a,l
    inc a
    xor l
    and 0x1F
    xor l
    ld l,a
xm_pp_e:
    ld a,e
    sub ixl
    jr c,xm_pp_s
    ld e,a
    djnz xm_pp
    ret
xm_pp_s:
    add a,ixh
    ld e,a
    inc d
    inc h
    ld a,h
    and 7
    jr nz,xm_pp_k
    ld a,l
    and 0x1F
    ld l,d
    ld h,ROWLO
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
xm_pp_k:
    djnz xm_pp
    ret

; ---- x-major, x -, y +
xm_np:
xm_np_op:
    ld a,(hl)
    xor c
    ld (hl),a
xm_np_jx:
    rlc c
    jr nc,xm_np_e
    ld a,l
    dec a
    xor l
    and 0x1F
    xor l
    ld l,a
xm_np_e:
    ld a,e
    sub ixl
    jr c,xm_np_s
    ld e,a
    djnz xm_np
    ret
xm_np_s:
    add a,ixh
    ld e,a
    inc d
    inc h
    ld a,h
    and 7
    jr nz,xm_np_k
    ld a,l
    and 0x1F
    ld l,d
    ld h,ROWLO
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
xm_np_k:
    djnz xm_np
    ret

; ---- x-major, x +, y -
xm_pn:
xm_pn_op:
    ld a,(hl)
    xor c
    ld (hl),a
xm_pn_jx:
    rrc c
    jr nc,xm_pn_e
    ld a,l
    inc a
    xor l
    and 0x1F
    xor l
    ld l,a
xm_pn_e:
    ld a,e
    sub ixl
    jr c,xm_pn_s
    ld e,a
    djnz xm_pn
    ret
xm_pn_s:
    add a,ixh
    ld e,a
    dec d
    ld a,h
    dec h
    and 7
    jr nz,xm_pn_k
    ld a,l
    and 0x1F
    ld l,d
    ld h,ROWLO
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
xm_pn_k:
    djnz xm_pn
    ret

; ---- x-major, x -, y -
xm_nn:
xm_nn_op:
    ld a,(hl)
    xor c
    ld (hl),a
xm_nn_jx:
    rlc c
    jr nc,xm_nn_e
    ld a,l
    dec a
    xor l
    and 0x1F
    xor l
    ld l,a
xm_nn_e:
    ld a,e
    sub ixl
    jr c,xm_nn_s
    ld e,a
    djnz xm_nn
    ret
xm_nn_s:
    add a,ixh
    ld e,a
    dec d
    ld a,h
    dec h
    and 7
    jr nz,xm_nn_k
    ld a,l
    and 0x1F
    ld l,d
    ld h,ROWLO
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
xm_nn_k:
    djnz xm_nn
    ret

; ---- y-major, x +, y +
ym_pp:
ym_pp_op:
    ld a,(hl)
    xor c
    ld (hl),a
    inc d
    inc h
    ld a,h
    and 7
    jr nz,ym_pp_e
    ld a,l
    and 0x1F
    ld l,d
    ld h,ROWLO
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
ym_pp_e:
    ld a,e
    sub ixl
    jr c,ym_pp_s
    ld e,a
    djnz ym_pp
    ret
ym_pp_s:
    add a,ixh
    ld e,a
ym_pp_jx:
    rrc c
    jr nc,ym_pp_k
    ld a,l
    inc a
    xor l
    and 0x1F
    xor l
    ld l,a
ym_pp_k:
    djnz ym_pp
    ret

; ---- y-major, x -, y +
ym_np:
ym_np_op:
    ld a,(hl)
    xor c
    ld (hl),a
    inc d
    inc h
    ld a,h
    and 7
    jr nz,ym_np_e
    ld a,l
    and 0x1F
    ld l,d
    ld h,ROWLO
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
ym_np_e:
    ld a,e
    sub ixl
    jr c,ym_np_s
    ld e,a
    djnz ym_np
    ret
ym_np_s:
    add a,ixh
    ld e,a
ym_np_jx:
    rlc c
    jr nc,ym_np_k
    ld a,l
    dec a
    xor l
    and 0x1F
    xor l
    ld l,a
ym_np_k:
    djnz ym_np
    ret

; ---- y-major, x +, y -
ym_pn:
ym_pn_op:
    ld a,(hl)
    xor c
    ld (hl),a
    dec d
    ld a,h
    dec h
    and 7
    jr nz,ym_pn_e
    ld a,l
    and 0x1F
    ld l,d
    ld h,ROWLO
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
ym_pn_e:
    ld a,e
    sub ixl
    jr c,ym_pn_s
    ld e,a
    djnz ym_pn
    ret
ym_pn_s:
    add a,ixh
    ld e,a
ym_pn_jx:
    rrc c
    jr nc,ym_pn_k
    ld a,l
    inc a
    xor l
    and 0x1F
    xor l
    ld l,a
ym_pn_k:
    djnz ym_pn
    ret

; ---- y-major, x -, y -
ym_nn:
ym_nn_op:
    ld a,(hl)
    xor c
    ld (hl),a
    dec d
    ld a,h
    dec h
    and 7
    jr nz,ym_nn_e
    ld a,l
    and 0x1F
    ld l,d
    ld h,ROWLO
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
ym_nn_e:
    ld a,e
    sub ixl
    jr c,ym_nn_s
    ld e,a
    djnz ym_nn
    ret
ym_nn_s:
    add a,ixh
    ld e,a
ym_nn_jx:
    rlc c
    jr nc,ym_nn_k
    ld a,l
    dec a
    xor l
    and 0x1F
    xor l
    ld l,a
ym_nn_k:
    djnz ym_nn
    ret

; loop for (x negative) | (y negative) << 1 | (y major) << 2
loop_tab:
    defw xm_pp
    defw xm_np
    defw xm_pn
    defw xm_nn
    defw ym_pp
    defw ym_np
    defw ym_pn
    defw ym_nn
; the pixel operation of each loop (patched: xor c / or c / and c)
op_tab:
    defw xm_pp_op + 1
    defw xm_np_op + 1
    defw xm_pn_op + 1
    defw xm_nn_op + 1
    defw ym_pp_op + 1
    defw ym_np_op + 1
    defw ym_pn_op + 1
    defw ym_nn_op + 1
    defw pl_op + 1
; the carry test after each mask rotate (jr nc; jr c for the inverted CLEAR mask)
jx_tab:
    defw xm_pp_jx + 2
    defw xm_np_jx + 2
    defw xm_pn_jx + 2
    defw xm_nn_jx + 2
    defw ym_pp_jx + 2
    defw ym_np_jx + 2
    defw ym_pn_jx + 2
    defw ym_nn_jx + 2

SECTION data_user
cur_mode:     defb 0            ; the loops start as XOR
mask_inv:     defb 0

SECTION bss_user
_line_x0:     defb 0
_line_y0:     defb 0
_line_x1:     defb 0
_line_y1:     defb 0
_line_mode:   defb 0
_line_skip:   defb 0
_line_signed: defb 0
ed_adx:       defb 0
ed_ady:       defb 0
ed_dir:       defb 0
prev_x:       defb 0
prev_y:       defb 0
lp_left:      defb 0
pd_left:      defb 0
pd_rec:       defw 0
pd_idx:       defb 0
ps_left:      defb 0
_pd_x:        defb 0
_pd_y:        defb 0
_pd_ptr:      defw 0
_ps_on:       defw 0
_ps_want:     defw 0
_ps_n:        defb 0
lp_next:      defw 0
_lp_cx:       defb 0
_lp_cy:       defb 0
_lp_pts:      defw 0
_lp_n:        defb 0
_lp_closed:   defb 0
_pl_ptr:      defw 0
_pl_n:        defb 0
