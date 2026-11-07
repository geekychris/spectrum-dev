; bitline.h: lines and points in XOR / OR / clear on a 1-bit bitmap reached
; through a row-address table (the 48K screen's zx_row by default, or an
; off-screen buffer's), clipped per pixel to a box. Bresenham.
;
; Lines inside the clip box take a register-only path (8-bit error term, the
; pixel operation and the x direction patched into the loop): ~45 T-states a
; pixel plus ~110 a row change, ~600 to set up. Lines that need clipping test
; every pixel (~300 T-states a pixel).

SECTION code_user

PUBLIC _bl_draw, _bl_draw_open, _bl_plot
PUBLIC _bl_rows, _bl_cx0, _bl_cx1, _bl_cy0, _bl_cy1, _bl_mode
EXTERN _zx_row

; void bl_draw(int16_t x0, int16_t y0, int16_t x1, int16_t y1)
_bl_draw:
    xor a
    jr line_entry

; the same without the last pixel (for polylines in XOR: each vertex once)
_bl_draw_open:
    ld a,1
line_entry:
    ld (lskip),a
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (lx),de                  ; x0
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (ly),de                  ; y0
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = x1
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = y1
    push de
    push bc
    ; clipping needed if an end lies outside the box
    ld hl,(lx)
    call outside_x
    jr c,clip_on
    ld hl,(ly)
    call outside_y
    jr c,clip_on
    pop hl                      ; x1
    push hl
    call outside_x
    jr c,clip_on
    pop bc
    pop hl                      ; y1
    push hl
    push bc
    call outside_y
clip_on:
    sbc a,a                     ; 0xFF if carry
    ld (lclip),a
    ; dx = |x1 - x0|, sx
    pop hl                      ; x1
    ld de,(lx)
    or a
    sbc hl,de
    call abs_step
    ld (ldx),hl
    ld (lsx),de
    ; dy = |y1 - y0|, sy
    pop hl                      ; y1
    ld de,(ly)
    or a
    sbc hl,de
    call abs_step
    ld (ldy),hl
    ld (lsy),de
    ld a,(lclip)
    or a
    jp z,fast_line
    ld hl,(ldy)
    ; major axis
    ld de,(ldx)
    or a
    sbc hl,de                   ; dy - dx
    jr nc,major_y
; |dx| > |dy|
    ex de,hl                    ; HL = dx
    call set_count
    ret z
    ld hl,(ldx)
    srl h
    rr l
    ld (lerr),hl
loop_x:
    call plot_cur
    ld hl,(lcount)
    dec hl
    ld (lcount),hl
    ld a,h
    or l
    ret z
    ld hl,(lx)
    ld de,(lsx)
    add hl,de
    ld (lx),hl
    ld hl,(lerr)
    ld de,(ldy)
    or a
    sbc hl,de
    ld (lerr),hl
    bit 7,h
    jr z,loop_x
    ld de,(ldx)
    add hl,de
    ld (lerr),hl
    ld hl,(ly)
    ld de,(lsy)
    add hl,de
    ld (ly),hl
    jr loop_x

major_y:
    ld hl,(ldy)
    call set_count
    ret z
    ld hl,(ldy)
    srl h
    rr l
    ld (lerr),hl
loop_y:
    call plot_cur
    ld hl,(lcount)
    dec hl
    ld (lcount),hl
    ld a,h
    or l
    ret z
    ld hl,(ly)
    ld de,(lsy)
    add hl,de
    ld (ly),hl
    ld hl,(lerr)
    ld de,(ldx)
    or a
    sbc hl,de
    ld (lerr),hl
    bit 7,h
    jr z,loop_y
    ld de,(ldy)
    add hl,de
    ld (lerr),hl
    ld hl,(lx)
    ld de,(lsx)
    add hl,de
    ld (lx),hl
    jr loop_y

; lcount = HL + 1 - lskip (pixels to plot); Z if none
set_count:
    inc hl
    ld a,(lskip)
    or a
    jr z,set_count_store
    dec hl
set_count_store:
    ld (lcount),hl
    ld a,h
    or l
    ret

; HL = |HL|, DE = +1 or -1 (its sign; +1 for 0)
abs_step:
    ld de,1
    bit 7,h
    ret z
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ld de,-1
    ret

; carry if HL (an x) is outside bl_cx0..bl_cx1 (uses B, C)
outside_x:
    ld a,h
    or a
    jr nz,is_out
    ld b,l
    ld a,(_bl_cx0)
    ld c,a
    ld a,b
    cp c                        ; x < cx0 -> carry
    ret c
    ld a,(_bl_cx1)
    cp b                        ; cx1 < x -> carry
    ret
is_out:
    scf
    ret

; carry if HL (a y) is outside bl_cy0..bl_cy1 (uses B, C)
outside_y:
    ld a,h
    or a
    jr nz,is_out
    ld b,l
    ld a,(_bl_cy0)
    ld c,a
    ld a,b
    cp c                        ; y < cy0 -> carry
    ret c
    ld a,(_bl_cy1)
    cp b                        ; cy1 < y -> carry
    ret

; void bl_plot(int16_t x, int16_t y): one pixel, clipped
_bl_plot:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (lx),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (ly),de
    ld a,0xFF
    ld (lclip),a

; plot (lx, ly) with bl_mode, testing the clip box if lclip
plot_cur:
    ld a,(lclip)
    or a
    jr z,plot_now
    ld hl,(lx)
    call outside_x
    ret c
    ld hl,(ly)
    call outside_y
    ret c
plot_now:
    ld a,(ly)
    ld l,a
    ld h,0
    add hl,hl
    ld de,(_bl_rows)
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = row address
    ld a,(lx)
    ld c,a
    rrca
    rrca
    rrca
    and 31
    add a,e
    ld e,a
    jr nc,plot_nc
    inc d
plot_nc:
    ld a,c
    and 7
    ld hl,bits
    add a,l
    ld l,a
    jr nc,plot_nc2
    inc h
plot_nc2:
    ld c,(hl)                   ; C = mask
    ex de,hl                    ; HL = byte
    ld a,(_bl_mode)
    or a
    jr nz,plot_or
    ld a,c
    xor (hl)
    ld (hl),a
    ret
plot_or:
    dec a
    jr nz,plot_clear
    ld a,c
    or (hl)
    ld (hl),a
    ret
plot_clear:
    ld a,c
    cpl
    and (hl)
    ld (hl),a
    ret

; ---------------------------------------------------------------- fast path
; the whole line is inside the clip box, so |dx|, |dy| <= 255
fast_line:
    ld a,(_bl_mode)           ; the pixel operation: A = mask, then
    or a                        ;  XOR: nop, xor (hl)  OR: nop, or (hl)
    ld bc,0xAE00                ;  CLEAR: cpl, and (hl)
    jr z,f_mode
    dec a
    ld bc,0xB600
    jr z,f_mode
    ld bc,0xA62F
f_mode:
    ld a,c
    ld (fx_op1),a
    ld (fy_op1),a
    ld a,b
    ld (fx_op2),a
    ld (fy_op2),a
    ld a,(lsx+1)                ; x direction: rrc c / inc hl / inc a, or rlc c / dec hl / dec a
    or a
    ld a,0x3C
    ld bc,0x0923
    jr z,f_xdir
    ld a,0x3D
    ld bc,0x012B
f_xdir:
    ld (fx_col),a
    ld (fy_col),a
    ld a,b
    ld (fx_rot+1),a
    ld (fy_rot+1),a
    ld a,c
    ld (fx_inc),a
    ld (fy_inc),a
    ld hl,2                     ; y direction: a row-table entry up or down
    ld a,(lsy+1)
    or a
    jr z,f_ydir
    ld hl,-2
f_ydir:
    ld (fsy2),hl
    ; HL = rows[y] + x / 8, C = the pixel's mask
    ld a,(ly)
    ld l,a
    ld h,0
    add hl,hl
    ld de,(_bl_rows)
    add hl,de
    ld (frow),hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld a,(lx)
    ld c,a
    rrca
    rrca
    rrca
    and 31
    ld (fcol),a
    add a,e
    ld e,a
    jr nc,f_nc
    inc d
f_nc:
    ld a,c
    and 7
    ld hl,bits
    add a,l
    ld l,a
    jr nc,f_nc2
    inc h
f_nc2:
    ld c,(hl)
    ex de,hl                    ; HL = the byte
    ld a,(ldx)
    ld d,a
    ld a,(ldy)
    ld e,a
    cp d
    jr nc,f_major_y
; major x: D = dy, E = error, B = pixels
    ld a,d
    ld (fdx),a
    ld d,e
    call f_count
    ret z
    ld a,(fdx)
    srl a
    ld e,a
fx_loop:
    ld a,c
fx_op1:
    nop
fx_op2:
    xor (hl)
    ld (hl),a
    dec b
    ret z
fx_rot:
    rrc c
    jr nc,fx_err
fx_inc:
    inc hl
    ld a,(fcol)
fx_col:
    inc a
    ld (fcol),a
fx_err:
    ld a,e
    sub d
    ld e,a
    jr nc,fx_loop
    ld a,(fdx)
    add a,e
    ld e,a
    call f_ystep
    jr fx_loop

; major y: D = dx, E = error
f_major_y:
    ld a,e
    ld (fdy),a
    call f_count
    ret z
    ld a,(fdy)
    srl a
    ld e,a
fy_loop:
    ld a,c
fy_op1:
    nop
fy_op2:
    xor (hl)
    ld (hl),a
    dec b
    ret z
    call f_ystep
    ld a,e
    sub d
    ld e,a
    jr nc,fy_loop
    ld a,(fdy)
    add a,e
    ld e,a
fy_rot:
    rrc c
    jr nc,fy_loop
fy_inc:
    inc hl
    ld a,(fcol)
fy_col:
    inc a
    ld (fcol),a
    jr fy_loop

; B = A + 1 - lskip pixels (0 = 256); Z if none
f_count:
    ld b,a
    ld a,(lskip)
    or a
    jr nz,f_count_open
    inc b
    ld a,1
    or a
    ret
f_count_open:
    ld a,b
    or a
    ret

; next row: HL = rows[y +- 1] + column (keeps BC, DE)
f_ystep:
    push de
    ld hl,(frow)
    ld de,(fsy2)
    add hl,de
    ld (frow),hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld a,(fcol)
    add a,e
    ld l,a
    ld a,d
    adc a,0
    ld h,a
    pop de
    ret

bits:
    defb 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01

SECTION data_user

_bl_rows: defw _zx_row
_bl_cx0:  defb 0
_bl_cx1:  defb 255
_bl_cy0:  defb 0
_bl_cy1:  defb 191
_bl_mode: defb 0

SECTION bss_user

lx:     defw 0
ly:     defw 0
ldx:    defw 0
ldy:    defw 0
lsx:    defw 0
lsy:    defw 0
lerr:   defw 0
lcount: defw 0
lclip:  defb 0
lskip:  defb 0
frow:   defw 0
fsy2:   defw 0
fcol:   defb 0
fdx:    defb 0
fdy:    defb 0
