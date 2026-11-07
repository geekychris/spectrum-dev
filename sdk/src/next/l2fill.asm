; l2fill.asm - convex polygons on the Layer 2 back buffer (l2fill.h): each
; edge is stepped once from its top vertex into a table of rows (the edges
; going down in one column, those going up in the other), then each row is
; filled between the two. Short spans by an unrolled loop, long ones by the
; zxnDMA. Uses the alternate registers (the SDK's interrupt saves them).

SECTION code_user

PUBLIC _l2f_begin, _l2f_end, _l2f_poly
PUBLIC _l2f_top, _l2f_bot
EXTERN _l2_back

ROWS        equ 192
SHORT_SPAN  equ 32              ; longer spans go to the DMA

; ---------------------------------------------------------------- begin / end
_l2f_begin:
    ld a,0x53
    ld bc,0x243B
    out (c),a
    inc b
    in a,(c)                    ; MMU slot 3 now
    ld (f_saved),a
    ld a,(_l2_back)
    add a,a
    ld (f_base),a               ; first 8K page of the back buffer
    ld a,0xFF
    ld (f_page),a
    ld hl,f_dma_init            ; the DMA set up for fills from f_col
    ld bc,(F_DMA_INIT_LEN << 8) | 0x6B
    otir
    ret

_l2f_end:
    ld a,(f_saved)
    nextreg 0x53,a
    ret

; ---------------------------------------------------------------- polygons
; void l2f_poly(const int16_t *xy, uint8_t n, uint8_t colour)
_l2f_poly:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)                   ; n
    cp 3
    ret c
    ld (f_n),a
    inc hl
    ld a,(hl)
    ld (f_col),a
    ld (f_xy),de
    push ix
    ld hl,0x7FFF
    ld (f_ymin),hl
    ld hl,0x8000
    ld (f_ymax),hl
    ld ix,(f_xy)                ; IX -> vertex i
    ld a,(f_n)
    ld (f_cnt),a
fe_loop:
    ld a,(f_cnt)                ; the next vertex (the first after the last)
    dec a
    jr nz,fe_mid
    ld de,(f_xy)
    jr fe_next
fe_mid:
    ld a,4
    push ix
    pop de
    ex de,hl
    add hl,a
    ex de,hl
fe_next:
    ld (f_nextv),de
    ld l,(ix+2)                 ; y0
    ld h,(ix+3)
    ld (f_y0),hl
    ld de,(f_ymin)              ; ymin = min(ymin, y0)
    call scmp
    jr nc,fe_nmin
    ld (f_ymin),hl
fe_nmin:
    ld de,(f_ymax)              ; ymax = max(ymax, y0)
    call scmp
    jr c,fe_nmax
    ld (f_ymax),hl
fe_nmax:
    ld hl,(f_nextv)
    inc hl
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = y1
    ld hl,(f_y0)
    call scmp                   ; y0 < y1: going down
    jr z,fe_skip                ; flat
    jr c,fe_down
    ; going up: from vertex i + 1 (top) to vertex i, into column 2
    ld (f_ty),de
    ld (f_by),hl
    ld hl,(f_nextv)
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (f_tx),de
    ld e,(ix+0)
    ld d,(ix+1)
    ld (f_bx),de
    ld a,2
    jr fe_go
fe_down:
    ld (f_ty),hl
    ld (f_by),de
    ld e,(ix+0)
    ld d,(ix+1)
    ld (f_tx),de
    ld hl,(f_nextv)
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (f_bx),de
    xor a
fe_go:
    ld (f_col2),a
    call raster
fe_skip:
    ld ix,(f_nextv)
    ld hl,f_cnt
    dec (hl)
    jp nz,fe_loop
    call fill
    pop ix
    ret

; signed compare HL with DE: carry if HL < DE, Z if equal (keeps HL, DE)
scmp:
    ld a,h
    xor d
    jp m,sc_diff
    push hl
    or a
    sbc hl,de
    pop hl
    ret
sc_diff:                        ; signs differ: HL < DE if HL is negative
    ld a,h
    rla                         ; carry = HL's sign
    ld a,0
    inc a                       ; NZ (inc keeps the carry)
    ret

; one edge, (f_tx, f_ty) above (f_bx, f_by), into the row table's column f_col2
raster:
    ld hl,(f_by)                ; by <= 0: above the screen
    dec hl
    bit 7,h
    ret nz
    ld hl,(f_ty)                ; ty >= ROWS: below it
    bit 7,h
    jr nz,ra_top_ok
    ld de,ROWS
    or a
    sbc hl,de
    ret nc
ra_top_ok:
    ; dy = by - ty, dx = bx - tx
    ld hl,(f_by)
    ld de,(f_ty)
    or a
    sbc hl,de
    ld (f_dy),hl
    ld hl,(f_bx)
    ld de,(f_tx)
    or a
    sbc hl,de
    ld a,h
    ld (f_sgn),a
    bit 7,h
    jr z,ra_dxpos
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a                      ; |dx|
ra_dxpos:
    ex de,hl                    ; DE = |dx|
    ld hl,(f_dy)
    ld a,h
    or a
    jp nz,ra_bigdy
    ld a,l
    cp 1
    jr nz,ra_rec
    xor a                       ; dy 1: slope = dx (16.8: int DE, frac 0)
    ld (f_sf),a
    ld (f_si),de
    jr ra_sign
ra_rec:
    ; slope = |dx| * (65536 / dy) >> 8, in 16.8: f_si (int), f_sf (frac)
    add a,a
    ld l,a
    ld h,0
    adc a,h
    sub l
    ld h,a                      ; HL = dy * 2 (dy < 256)
    ld bc,f_rec
    add hl,bc
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = rec = rh:rl (B = rh, C = rl)
    ; |dx| (D:E) * rec (B:C) >> 8 = (dl*rl >> 8) + dl*rh + dh*rl + (dh*rh << 8):
    ; a 24-bit sum T:H:L, the slope's int T:H and fraction L
    ld (f_dx),de
    ld d,c
    mul d,e                     ; dl * rl
    ld a,d
    ld (f_a0),a
    ld de,(f_dx)
    ld d,b
    mul d,e                     ; dl * rh
    ex de,hl
    ld de,(f_dx)
    ld e,c
    mul d,e                     ; dh * rl
    add hl,de
    ld a,0
    adc a,a
    ld (f_t),a                  ; T: the carry
    ld de,(f_dx)
    ld e,b
    mul d,e                     ; dh * rh, << 8
    ld a,h
    add a,e
    ld h,a
    ld a,(f_t)
    adc a,d
    ld (f_t),a
    ld a,(f_a0)                 ; + dl * rl >> 8
    add a,l
    ld l,a
    jr nc,ra_prod
    inc h
    jr nz,ra_prod
    ld a,(f_t)
    inc a
    ld (f_t),a
ra_prod:
    ld a,l
    ld (f_sf),a
    ld l,h
    ld a,(f_t)
    ld h,a
    ld (f_si),hl
ra_sign:
    ld a,(f_sgn)
    rla
    jr nc,ra_start
    ld a,(f_sf)                 ; negate the 24-bit slope
    neg
    ld (f_sf),a
    ld hl,(f_si)
    ld a,0
    sbc a,l
    ld l,a
    ld a,0
    sbc a,h
    ld h,a
    ld (f_si),hl
ra_start:
    ; the first row: ty, or 0 (stepping down to it)
    ld hl,(f_tx)
    ld (f_xi),hl
    ld a,0x80
    ld (f_xf),a
    ld hl,(f_ty)
    bit 7,h
    jr z,ra_y_ok
ra_clip:                        ; above the screen: step to row 0
    ld a,(f_xf)
    ld b,a
    ld a,(f_sf)
    add a,b
    ld (f_xf),a
    ld de,(f_si)
    ld hl,(f_xi)
    adc hl,de
    ld (f_xi),hl
    ld hl,(f_ty)
    inc hl
    ld (f_ty),hl
    bit 7,h
    jr nz,ra_clip
ra_y_ok:
    ; rows ty .. min(by, ROWS) - 1
    ld hl,(f_by)
    ld de,ROWS
    call scmp
    jr c,ra_end_ok
    ex de,hl
ra_end_ok:
    ld de,(f_ty)
    or a
    sbc hl,de
    ld b,l                      ; rows (1..192)
    ld a,e                      ; ptr = f_tab + ty * 4 + column
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    ld a,(f_col2)
    add hl,a
    ld de,f_tab
    add hl,de
    exx
    ld hl,(f_xi)
    ld a,(f_xf)
    ld c,a
    ld de,(f_si)
    ld a,(f_sf)
    ld b,a
    exx
ra_row:                         ; main: HL = ptr, B = rows; alt: HL x, C frac, DE slope, B frac
    exx
    push hl
    ld a,c
    add a,b
    ld c,a
    adc hl,de
    exx
    pop de
    ld (hl),e
    inc hl
    ld (hl),d
    ld a,3
    add hl,a
    djnz ra_row
    ret

ra_bigdy:
    ; dy >= 256: slope = (|dx| << 8) / dy, by shifts (rare)
    ld (f_div),hl
    ld h,e                      ; HL:0 = |dx| << 8 as a 24-bit dividend, quotient built in it
    ld l,0
    ld c,d                      ; C:H:L = dividend (C high)
    xor a
    ld (f_sf),a
    ld de,0                     ; DE = remainder
    ld b,24
rb_loop:
    add hl,hl
    rl c
    rl e
    rl d
    push hl
    ex de,hl
    ld de,(f_div)
    or a
    sbc hl,de
    jr c,rb_no
    ex de,hl                    ; keep the remainder
    pop hl
    inc l                       ; quotient bit
    jr rb_next
rb_no:
    add hl,de
    ex de,hl
    pop hl
rb_next:
    djnz rb_loop
    ld a,l                      ; quotient C:H:L = int H? (16.8: int = C:H, frac = L)
    ld (f_sf),a
    ld l,h
    ld h,c
    ld (f_si),hl
    jp ra_sign

; the rows of the table from max(ymin, top) to min(ymax, bot) - 1
fill:
    ld hl,(f_ymin)
    ld a,(_l2f_top)
    ld e,a
    ld d,0
    call scmp
    jr nc,fl_y0
    ex de,hl
fl_y0:
    ld (f_y),hl                 ; first row
    ex de,hl
    ld hl,(f_ymax)
    ld a,(_l2f_bot)
    ld c,a
    ld b,0
    push de
    ld d,b
    ld e,c
    call scmp
    pop de
    jr c,fl_y1
    ld h,b
    ld l,c
fl_y1:
    or a
    sbc hl,de                   ; rows
    ret c
    ret z
    bit 7,h
    ret nz
    ld a,l
    ld (f_rows),a
    ld a,e                      ; ptr = f_tab + y * 4
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    ld de,f_tab
    add hl,de
    ld (f_p),hl
    ld a,(f_y)
    call map_row
fl_row:
    ld hl,(f_p)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld (f_p),hl
    ld h,b                      ; the right end is the larger (the values are small: no overflow)
    ld l,c
    or a
    sbc hl,de
    jp p,fl_ordered
    ld h,d
    ld l,e
    ld d,b
    ld e,c
    ld b,h
    ld c,l
fl_ordered:                     ; DE = left, BC = right
    ld a,b
    or a
    jr nz,fl_rbig
fl_r8:
    ld a,d
    or a
    jr nz,fl_lbig
fl_l8:
    ld a,c
    sub e                       ; count - 1
    cp SHORT_SPAN
    jr nc,fl_dma
    ; unrolled: jump into the run of (ld (hl),a; inc l) at its end - count
    inc a
    add a,a
    neg
    ld c,a
    ld b,0xFF                   ; BC = -2 * count
    ld hl,fl_unroll_end
    add hl,bc
    ld (fl_jump + 1),hl
    ld a,(f_rowhi)
    ld h,a
    ld l,e                      ; HL = the first pixel
    ld a,(f_col)
fl_jump:
    jp 0                        ; (the entry, written above)
fl_rbig:
    bit 7,b
    jr nz,fl_next               ; right < 0
    ld c,255                    ; right > 255
    jr fl_r8
fl_lbig:
    bit 7,d
    jr z,fl_next                ; left > 255
    ld e,0                      ; left < 0
    jr fl_l8
fl_dma:
    inc a
    ld l,a
    ld h,0
    jr nz,fl_dlen
    inc h                       ; 256
fl_dlen:
    ld (f_dma_len),hl
    ld a,(f_rowhi)
    ld h,a
    ld l,e
    ld (f_dma_dst),hl
    ld hl,f_dma_prog
    ld bc,(F_DMA_PROG_LEN << 8) | 0x6B
    otir
fl_next:
    ld hl,f_rows
    dec (hl)
    ret z
    ld hl,f_rowhi               ; the next row: 0x60 + (y & 31), a new page every 32
    inc (hl)
    ld a,(hl)
    and 0x1F
    jp nz,fl_row
    ld (hl),0x60
    ld a,(f_page)
    inc a
    ld (f_page),a
    nextreg 0x53,a
    jp fl_row

; the run: SHORT_SPAN x (ld (hl),a; inc l), then back into the row loop
fl_unroll:
    REPT SHORT_SPAN
    ld (hl),a
    inc l
    ENDR
fl_unroll_end:
    jp fl_next

; map the page of row A, set f_rowhi (keeps DE, HL)
map_row:
    ld b,a
    rlca
    rlca
    rlca
    and 7
    ld c,a
    ld a,(f_base)
    add a,c
    ld c,a
    ld a,(f_page)
    cp c
    jr z,mr_same
    ld a,c
    ld (f_page),a
    nextreg 0x53,a
mr_same:
    ld a,b
    and 31
    or 0x60
    ld (f_rowhi),a
    ret

SECTION data_user
_l2f_top:  defb 0
_l2f_bot:  defb ROWS

f_dma_init:
    defb 0x83                   ; WR6: disable
    defb 0x7D                   ; WR0: A -> B, A address + length follow
    defw f_col                  ;      A: the colour byte
    defw 1
    defb 0x64, 0x02             ; WR1: A memory, fixed; 2-cycle timing
    defb 0x50, 0x02             ; WR2: B memory, incrementing; 2-cycle timing
    defb 0x82                   ; WR5: stop at the end
f_dma_init_end:
F_DMA_INIT_LEN equ f_dma_init_end - f_dma_init
f_dma_prog:
    defb 0x65                   ; WR0: A -> B, length follows
f_dma_len:
    defw 0
    defb 0xAD                   ; WR4: burst, B address follows
f_dma_dst:
    defw 0
    defb 0xCF                   ; WR6: load
    defb 0x87                   ; WR6: enable
f_dma_prog_end:
F_DMA_PROG_LEN equ f_dma_prog_end - f_dma_prog

SECTION rodata_user
; 65536 / dy for dy 2..255 (0 and 1 unused)
f_rec:
    defw 0, 0, 32768, 21845, 16384, 13107, 10922, 9362
    defw 8192, 7281, 6553, 5957, 5461, 5041, 4681, 4369
    defw 4096, 3855, 3640, 3449, 3276, 3120, 2978, 2849
    defw 2730, 2621, 2520, 2427, 2340, 2259, 2184, 2114
    defw 2048, 1985, 1927, 1872, 1820, 1771, 1724, 1680
    defw 1638, 1598, 1560, 1524, 1489, 1456, 1424, 1394
    defw 1365, 1337, 1310, 1285, 1260, 1236, 1213, 1191
    defw 1170, 1149, 1129, 1110, 1092, 1074, 1057, 1040
    defw 1024, 1008, 992, 978, 963, 949, 936, 923
    defw 910, 897, 885, 873, 862, 851, 840, 829
    defw 819, 809, 799, 789, 780, 771, 762, 753
    defw 744, 736, 728, 720, 712, 704, 697, 689
    defw 682, 675, 668, 661, 655, 648, 642, 636
    defw 630, 624, 618, 612, 606, 601, 595, 590
    defw 585, 579, 574, 569, 564, 560, 555, 550
    defw 546, 541, 537, 532, 528, 524, 520, 516
    defw 512, 508, 504, 500, 496, 492, 489, 485
    defw 481, 478, 474, 471, 468, 464, 461, 458
    defw 455, 451, 448, 445, 442, 439, 436, 434
    defw 431, 428, 425, 422, 420, 417, 414, 412
    defw 409, 407, 404, 402, 399, 397, 394, 392
    defw 390, 387, 385, 383, 381, 378, 376, 374
    defw 372, 370, 368, 366, 364, 362, 360, 358
    defw 356, 354, 352, 350, 348, 346, 344, 343
    defw 341, 339, 337, 336, 334, 332, 330, 329
    defw 327, 326, 324, 322, 321, 319, 318, 316
    defw 315, 313, 312, 310, 309, 307, 306, 304
    defw 303, 302, 300, 299, 297, 296, 295, 293
    defw 292, 291, 289, 288, 287, 286, 284, 283
    defw 282, 281, 280, 278, 277, 276, 275, 274
    defw 273, 271, 270, 269, 268, 267, 266, 265
    defw 264, 263, 262, 261, 260, 259, 258, 257

SECTION bss_user
f_tab:   defs ROWS * 4          ; per row: x of an edge going down, x of one going up
f_saved: defb 0
f_base:  defb 0
f_page:  defb 0
f_rowhi: defb 0
f_col:   defb 0
f_col2:  defb 0
f_n:     defb 0
f_cnt:   defb 0
f_sgn:   defb 0
f_rows:  defb 0
f_xf:    defb 0
f_sf:    defb 0
f_a0:    defb 0
f_t:     defb 0
f_xy:    defw 0
f_nextv: defw 0
f_y0:    defw 0
f_ymin:  defw 0
f_ymax:  defw 0
f_tx:    defw 0
f_ty:    defw 0
f_bx:    defw 0
f_by:    defw 0
f_dy:    defw 0
f_si:    defw 0
f_xi:    defw 0
f_div:   defw 0
f_dx:    defw 0
f_y:     defw 0
f_p:     defw 0
