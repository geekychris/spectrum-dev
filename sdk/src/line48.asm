; Lines, wireframe polygons and point lists on the 48K screen (line48.h).
;
; Bresenham with the octant and the pixel operation patched into the two
; inner loops (x-major, y-major) before each line. Registers in the loops:
; HL = screen byte, C = pixel mask, D = y (0..255, the row table wraps it
; mod 192), E = error, B = pixels left.
;
; Row tables (line48.c builds them): 0xF200 + y = low byte of the address of
; line y mod 192 (column 0), 0xF300 + y = high byte; y 192..223 are lines
; 0..31 and 224..255 are lines 160..191 (-32..-1).
;
; Moving a line down within a character cell is INC H; across cells (and
; across the wrap) the address comes from the table.

SECTION code_user

PUBLIC _line_x0, _line_y0, _line_x1, _line_y1, _line_mode, _line_skip, _line_signed
PUBLIC _line_core, _lp_core, _pl_core
PUBLIC _lp_cx, _lp_cy, _lp_pts, _lp_n, _lp_closed
PUBLIC _pl_ptr, _pl_n

defc ROWLO = 0xF2

; ------------------------------------------------------------------ mode

; copy the 3-byte pixel operation for line_mode into both loops and plot_list
set_mode:
    ld a,(_line_mode)
    ld hl,op_xor
    or a
    jr z,sm_set
    ld hl,op_or
    dec a
    jr z,sm_set
    ld hl,op_clear
sm_set:
    push hl
    ld de,xm_op
    ldi
    ldi
    ldi
    pop hl
    push hl
    ld de,ym_op
    ldi
    ldi
    ldi
    pop hl
    ld de,pl_op
    ldi
    ldi
    ldi
    ret

op_xor:   defb 0x7E, 0xA9, 0x00     ; ld a,(hl) / xor c / nop
op_or:    defb 0x7E, 0xB1, 0x00     ; ld a,(hl) / or c  / nop
op_clear: defb 0x79, 0x2F, 0xA6     ; ld a,c / cpl / and (hl)

bitmask:  defb 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01

; ------------------------------------------------------------------ one line

; line_x0..y1, line_skip (leave out the last pixel), line_signed (differences
; as -128..127, for wrapped coordinates; else 0..255 with the sign from the
; comparison)
_line_core:
    call set_mode
line_go:
    ; ---- x: |dx| and the direction
    ld a,(_line_x1)
    ld hl,_line_x0
    sub (hl)
    ld e,a
    sbc a,a
    ld d,a                      ; D = FF if x1 < x0 (unsigned)
    ld a,(_line_signed)
    or a
    jr z,lg_xu
    ld a,e
    add a,a
    sbc a,a
    ld d,a                      ; D = FF if dx < 0 (signed)
lg_xu:
    ld a,d
    or a
    ld a,e
    jr z,lg_xpos
    neg
lg_xpos:
    ld (adx),a
    ld a,d
    or a
    ld a,0x09                   ; rrc c: right
    ld b,0x3C                   ; inc a
    jr z,lg_xr
    ld a,0x01                   ; rlc c: left
    ld b,0x3D                   ; dec a
lg_xr:
    ld (xm_rot + 1),a
    ld (ym_rot + 1),a
    ld a,b
    ld (xm_col),a
    ld (ym_col),a
    ; ---- y
    ld a,(_line_y1)
    ld hl,_line_y0
    sub (hl)
    ld e,a
    sbc a,a
    ld d,a
    ld a,(_line_signed)
    or a
    jr z,lg_yu
    ld a,e
    add a,a
    sbc a,a
    ld d,a
lg_yu:
    ld a,d
    or a
    ld a,e
    jr z,lg_ypos
    neg
lg_ypos:
    ld (ady),a
    ld a,d
    or a
    ld a,0x14                   ; inc d
    ld bc,0x7C24                ; inc h / ld a,h
    jr z,lg_yd
    ld a,0x15                   ; dec d
    ld bc,0x257C                ; ld a,h / dec h
lg_yd:
    ld (xm_ystep),a
    ld (ym_ystep),a
    ld (xm_ysh),bc
    ld (ym_ysh),bc
    ; ---- major axis, count and error
    ld a,(ady)
    ld b,a
    ld a,(adx)
    cp b
    jr c,lg_ymaj
    ld (xm_adx + 1),a
    ld c,a
    ld a,b
    ld (xm_ady + 1),a
    ld a,c
    ld hl,xm_loop
    jr lg_count
lg_ymaj:
    ld (ym_adx + 1),a
    ld a,b
    ld (ym_ady + 1),a
    ld hl,ym_loop
lg_count:
    ld (lg_jump + 1),hl
    ld c,a
    srl a
    ld (err),a
    ld a,c
    inc a                       ; pixels = major + 1 (0 = 256)
    ld b,a
    ld a,(_line_skip)
    or a
    jr z,lg_noskip
    dec b
    ret z                       ; a one-pixel line without its last pixel
lg_noskip:
    push bc
    ; ---- mask and start address
    ld a,(_line_x0)
    and 7
    ld hl,bitmask
    add a,l
    ld l,a
    adc a,h
    sub l
    ld h,a
    ld c,(hl)
    ld a,(_line_y0)
    ld d,a
    ld l,a
    ld h,ROWLO
    ld a,(_line_x0)
    rrca
    rrca
    rrca
    and 0x1F
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
    ld a,(err)
    ld e,a
    pop af
    ld b,a
lg_jump:
    jp xm_loop

; ---- x-major: a pixel every step along x, y when the error runs out
xm_loop:
xm_op:
    ld a,(hl)
    xor c
    nop
    ld (hl),a
xm_rot:
    rrc c
    jr nc,xm_nocol
    ld a,l
xm_col:
    inc a
    xor l
    and 0x1F
    xor l
    ld l,a
xm_nocol:
    ld a,e
xm_ady:
    sub 0
    jr c,xm_ys
    ld e,a
    djnz xm_loop
    ret
xm_ys:
xm_adx:
    add a,0
    ld e,a
xm_ystep:
    inc d
xm_ysh:
    inc h
    ld a,h
    and 7
    jr nz,xm_ok
    ld a,l
    and 0x1F
    ld l,d
    ld h,ROWLO
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
xm_ok:
    djnz xm_loop
    ret

; ---- y-major
ym_loop:
ym_op:
    ld a,(hl)
    xor c
    nop
    ld (hl),a
ym_ystep:
    inc d
ym_ysh:
    inc h
    ld a,h
    and 7
    jr nz,ym_ok
    ld a,l
    and 0x1F
    ld l,d
    ld h,ROWLO
    or (hl)
    inc h
    ld h,(hl)
    ld l,a
ym_ok:
    ld a,e
ym_adx:
    sub 0
    jr c,ym_xs
    ld e,a
    djnz ym_loop
    ret
ym_xs:
ym_ady:
    add a,0
    ld e,a
ym_rot:
    rrc c
    jr nc,ym_next
    ld a,l
ym_col:
    inc a
    xor l
    and 0x1F
    xor l
    ld l,a
ym_next:
    djnz ym_loop
    ret

; ------------------------------------------------------------------ polygon

; lp_n vertices (x, y signed bytes) at lp_pts around (lp_cx, lp_cy), wrapped;
; every edge without its last pixel; lp_closed = back to the first vertex,
; else the last vertex is plotted
_lp_core:
    call set_mode
    ld a,1
    ld (_line_signed),a
    ld (_line_skip),a
    ld hl,(_lp_pts)
    ld a,(_lp_cx)
    add a,(hl)
    ld (first_x),a
    ld (_line_x0),a
    inc hl
    ld a,(_lp_cy)
    add a,(hl)
    ld (first_y),a
    ld (_line_y0),a
    inc hl
    ld a,(_lp_n)
    dec a
    jr z,lp_last
    ld b,a
lp_loop:
    push bc
    ld a,(_lp_cx)
    add a,(hl)
    ld (_line_x1),a
    inc hl
    ld a,(_lp_cy)
    add a,(hl)
    ld (_line_y1),a
    inc hl
    ld (lp_next),hl
    call line_go
    ld a,(_line_x1)
    ld (_line_x0),a
    ld a,(_line_y1)
    ld (_line_y0),a
    ld hl,(lp_next)
    pop bc
    djnz lp_loop
lp_last:
    ld a,(_lp_closed)
    or a
    jr z,lp_open
    ld a,(first_x)
    ld (_line_x1),a
    ld a,(first_y)
    ld (_line_y1),a
    jp line_go
lp_open:
    xor a
    ld (_line_skip),a
    ld a,(_line_x0)
    ld (_line_x1),a
    ld a,(_line_y0)
    ld (_line_y1),a
    jp line_go

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
    ld a,(de)
    ld c,a
pl_op:
    ld a,(hl)
    xor c
    nop
    ld (hl),a
    pop hl
pl_skip:
    djnz pl_loop
    ret

SECTION bss_user
_line_x0:     defb 0
_line_y0:     defb 0
_line_x1:     defb 0
_line_y1:     defb 0
_line_mode:   defb 0
_line_skip:   defb 0
_line_signed: defb 0
adx:          defb 0
ady:          defb 0
err:          defb 0
first_x:      defb 0
first_y:      defb 0
lp_next:      defw 0
_lp_cx:       defb 0
_lp_cy:       defb 0
_lp_pts:      defw 0
_lp_n:        defb 0
_lp_closed:   defb 0
_pl_ptr:      defw 0
_pl_n:        defb 0
