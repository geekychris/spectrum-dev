; l2poly.asm - convex polygons and spans on the Layer 2 back buffer (l2poly.h).
;
; The back buffer is the 16K bank l2_back (nextl2.c): 256 x 192, 32 rows to
; an 8K page, mapped into MMU slot 3 (0x6000) as the rows need it.

SECTION code_user

PUBLIC _l2p_begin, _l2p_end, _l2p_poly, _l2p_span, _l2p_band, _l2p_rect
PUBLIC _l2p_top, _l2p_bot, _l2p_xmin, _l2p_xmax
PUBLIC l2p_fill                 ; for other asm: B = y, DE = x0, HL = x1 (clipped here), colour in l2p_col
EXTERN _l2_back
EXTERN l_z80n_muls_32_16x16     ; HL * DE -> DEHL (signed)
EXTERN _l2p_slope_div           ; C: int32_t l2p_slope_div(int16_t dx, int16_t dy)  (dx << 16) / dy
EXTERN _l2p_x_at
EXTERN _l2p_recip               ; C: uint16_t l2p_recip[256], 65536 / d                ; C: int32_t l2p_x_at(int16_t x, int16_t dx, int16_t dy, int16_t t)

SPAN_DMA equ 40                 ; spans this long or longer go to the DMA

; ---------------------------------------------------------------- begin / end

_l2p_begin:
    ld a,0x53
    ld bc,0x243B
    out (c),a
    inc b
    in a,(c)                    ; MMU slot 3 now
    ld (l2p_saved),a
    ld a,(_l2_back)
    add a,a
    ld (l2p_base),a             ; first 8K page of the back buffer
    ld a,0xFF
    ld (l2p_page),a             ; nothing mapped yet
    ; the zxnDMA set up for fills from l2p_col; a span then sends its length
    ; and destination only (don't use the DMA elsewhere before l2p_end)
    ld hl,dma_init
    ld bc,(DMA_INIT_LEN << 8) | 0x6B
    otir
    ret

_l2p_end:
    ld a,(l2p_saved)
    nextreg 0x53,a
    ret

; ---------------------------------------------------------------- spans

; void l2p_span(uint8_t y, int16_t x0, int16_t x1, uint8_t colour)
_l2p_span:
    ld hl,2
    add hl,sp
    ld b,(hl)                   ; y
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = x0
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; HL = x1
    ld a,(_l2p_top)
    ld c,a
    ld a,b
    cp c
    ret c                       ; y < top
    ld a,(_l2p_bot)
    ld c,a
    ld a,b
    cp c
    ret nc                      ; y >= bot
    push hl
    ld hl,9
    add hl,sp
    ld a,(hl)
    ld (l2p_col),a
    pop hl
    ; fall into l2p_fill

; B = y (on screen), DE = x0, HL = x1 (int16, inclusive); colour in l2p_col.
; Clips to l2p_xmin..l2p_xmax and fills. Keeps IX, IY. Uses A, BC, DE, HL.
l2p_fill:
    ; x1 < xmin or x1 negative: nothing
    bit 7,h
    ret nz
    ld a,h
    or a
    jr nz,lf_x1big
    ld a,(_l2p_xmin)
    ld c,a
    ld a,l
    cp c
    ret c
    ld a,(_l2p_xmax)
    cp l
    jr nc,lf_x1ok               ; x1 <= xmax
lf_x1big:
    ld a,(_l2p_xmax)
    ld l,a
lf_x1ok:                        ; L = x1, clipped
    bit 7,d
    jr nz,lf_x0small            ; x0 < 0
    ld a,d
    or a
    ret nz                      ; x0 > 255
    ld a,(_l2p_xmin)
    cp e
    jr c,lf_x0ok
    jr z,lf_x0ok
lf_x0small:
    ld a,(_l2p_xmin)
    ld e,a
lf_x0ok:                        ; E = x0, clipped
    ld a,l
    sub e
    ret c                       ; x0 > x1
    inc a                       ; A = length (1..255); 0 = 256
    ld c,a
    ; map the row's page
    ld a,b
    rlca
    rlca
    rlca
    and 7                       ; y >> 5
    ld hl,l2p_base
    add a,(hl)
    ld hl,l2p_page
    cp (hl)
    jr z,lf_mapped
    ld (hl),a
    nextreg 0x53,a
lf_mapped:
    ld a,b
    and 31
    or 0x60
    ld d,a                      ; DE = 0x6000 + (y & 31) * 256 + x0
    ld a,c
    or a
    jr z,lf_dma                 ; 256
    cp SPAN_DMA
    jr nc,lf_dma
    ; unrolled: enter the block 2 * length bytes before its end
    ld hl,lf_unroll_end
    add a,a
    jr nc,lf_nc
    dec h
lf_nc:
    neg
    jr z,lf_go
    add a,l
    ld l,a
    jr c,lf_go
    dec h
lf_go:
    ld a,(l2p_col)
    jp (hl)

lf_dma:
    ; zxnDMA: fill C bytes (0 = 256) at DE with l2p_col
    ld (dma_dst),de
    ld b,0
    ld a,c
    or a
    jr nz,lf_len
    inc b                       ; 256
lf_len:
    ld (dma_len),bc
    ld hl,dma_prog
    ld bc,(DMA_PROG_LEN << 8) | 0x6B
    otir
    ret

; ---------------------------------------------------------------- polygons

; void l2p_poly(const int16_t *xy, uint8_t n, uint8_t colour)
_l2p_poly:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (lp_pv),de                  ; vertices
    inc hl
    ld a,(hl)
    cp 3
    ret c
    ld (lp_pn),a
    inc hl
    ld a,(hl)
    ld (l2p_col),a
    push ix
    ; top (min y) and bottom (max y)
    ld ix,(lp_pv)
    ld l,(ix+2)
    ld h,(ix+3)
    ld (lp_ymin),hl
    ld (lp_ymax),hl
    xor a
    ld (lp_itop),a
    ld c,0                      ; index
pp_find:
    inc c
    ld a,(lp_pn)
    cp c
    jr z,pp_found
    ld de,4
    add ix,de
    ld l,(ix+2)
    ld h,(ix+3)                 ; y of vertex C
    push hl
    ld de,(lp_ymin)
    call cmp_hl_de              ; signed: carry if HL < DE
    jr nc,pp_notmin
    ld (lp_ymin),hl
    ld a,c
    ld (lp_itop),a
pp_notmin:
    pop hl
    ld de,(lp_ymax)
    call cmp_hl_de
    jr c,pp_find
    ld (lp_ymax),hl
    jr pp_find
pp_found:
    ; visible rows: start = max(lp_ymin, top), stop = min(lp_ymax, bot)
    ld hl,(lp_ymin)
    ld de,(lp_ymax)
    or a
    sbc hl,de
    jp z,pp_done                ; flat
    ld a,(_l2p_bot)
    ld e,a
    ld d,0
    ld hl,(lp_ymin)
    call cmp_hl_de
    jp nc,pp_done               ; lp_ymin >= bot
    ld a,(_l2p_top)
    ld e,a
    ld d,0
    ld hl,(lp_ymax)
    call cmp_hl_de
    jp c,pp_done                ; lp_ymax < top
    jp z,pp_done                ; lp_ymax == top (rows end before top)
    ld hl,(lp_ymax)
    ld a,(_l2p_bot)
    ld e,a
    ld d,0
    call cmp_hl_de
    jr c,pp_stop_ok
    ex de,hl
pp_stop_ok:
    ld (lp_ystop),hl               ; rows < lp_ystop
    ld hl,(lp_ymin)
    ld a,(_l2p_top)
    ld e,a
    ld d,0
    call cmp_hl_de
    jr nc,pp_start_ok
    ex de,hl
pp_start_ok:
    ld (lp_ycur),hl                ; first row
    ; the two chains from the top: A forward, B backward
    ld a,(lp_itop)
    ld (cha_i),a
    ld (chb_i),a
    ld ix,cha
    ld a,1
    call chain_next
    ld ix,chb
    ld a,-1
    call chain_next
; the rows: chain A in HL (x), C (fraction), DE (slope), B (slope fraction);
; chain B in the same registers of the other set (16.8 fixed point)
    ld a,(lp_ystop)
    ld (pp_vars),a
    ld a,(lp_ycur)
    call row_map
    call ld_chains
    ld ix,pp_vars
pp_row:
    push bc
    push de
    push hl
    exx
    push hl
    exx
    pop de                      ; DE = x of B, HL = x of A
    ld a,h
    xor d
    jp p,pp_same
    bit 7,h                     ; signs differ: the negative one is the left
    jr z,pp_ordered
    ex de,hl
    jr pp_ordered
pp_same:
    ld a,l
    sub e
    ld a,h
    sbc a,d
    jr nc,pp_ordered
    ex de,hl
pp_ordered:                     ; DE = left, HL = right
    call ffill
    pop hl
    pop de
    pop bc
    ld a,c                      ; step A
    add a,b
    ld c,a
    adc hl,de
    exx
    ld a,c                      ; step B
    add a,b
    ld c,a
    adc hl,de
    exx
    ld a,(l2p_rowhi)            ; the next row's address (a new page every 32)
    inc a
    ld (l2p_rowhi),a
    and 31
    call z,pp_nextpage
    ld a,(lp_ycur)
    inc a
    ld (lp_ycur),a
    cp (ix+0)                   ; ystop
    jr z,pp_done
    cp (ix+1)                   ; chain A's next vertex
    jr z,pp_switch
    cp (ix+2)                   ; chain B's
    jp nz,pp_row
pp_switch:
    ; a chain reached its next vertex: save both, move it on, reload
    call st_chains
    ld a,(lp_ycur)
    cp (ix+1)
    jr nz,pp_sw_b
    ld ix,cha
    ld a,1
    call chain_next
    ld ix,pp_vars
pp_sw_b:
    ld a,(lp_ycur)
    cp (ix+2)
    jr nz,pp_sw_done
    ld ix,chb
    ld a,-1
    call chain_next
    ld ix,pp_vars
pp_sw_done:
    call ld_chains
    jp pp_row
pp_done:
    pop ix
    ret


; ---------------------------------------------------------------- bands

; void l2p_band(int16_t ytop, int16_t ybot, const int16_t *xtop, const int16_t *xbot,
;               uint8_t nr, const uint8_t *spans, uint8_t ns)
; Rails step in 16.8 (24 bits): x and slope, 3 bytes each, low byte first.
_l2p_band:
    push ix
    ld ix,0
    add ix,sp
    ld l,(ix+4)
    ld h,(ix+5)
    ld e,(ix+6)
    ld d,(ix+7)
    xor a
    ld (bd_swap),a
    call cmp_hl_de              ; ytop < ybot?
    jr c,bd_ord
    jp z,bd_done                ; no rows
    ex de,hl
    ld a,1
    ld (bd_swap),a
bd_ord:
    ld (bd_ytop),hl
    ld (bd_ybot),de
    ex de,hl
    or a
    sbc hl,de
    ld (bd_dy),hl               ; > 0
    ; y0 = max(ytop, top), y1 = min(ybot, bot)
    ld hl,(bd_ytop)
    ld a,(_l2p_top)
    ld e,a
    ld d,0
    call cmp_hl_de
    jr nc,bd_y0
    ex de,hl
bd_y0:
    ld (lp_ycur),hl
    ld hl,(bd_ybot)
    ld a,(_l2p_bot)
    ld e,a
    ld d,0
    call cmp_hl_de
    jr c,bd_y1
    ex de,hl
bd_y1:
    ld (lp_ystop),hl
    ld de,(lp_ycur)
    call cmp_hl_de              ; ystop <= ycur: nothing visible
    jp c,bd_done
    jp z,bd_done
    ; the rails
    ld l,(ix+8)
    ld h,(ix+9)
    ld (bd_src),hl
    ld l,(ix+10)
    ld h,(ix+11)
    ld (bd_src2),hl
    ld a,(ix+12)
    or a
    jp z,bd_done
    ld (bd_nr),a
    ld (bd_left),a
    ld hl,bd_rx
    ld (bd_dst),hl
bd_rail:
    ld hl,(bd_src)
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = x at ytop (as given)
    inc hl
    ld (bd_src),hl
    ld hl,(bd_src2)
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = x at ybot (as given)
    inc hl
    ld (bd_src2),hl
    ld a,(bd_swap)
    or a
    jr z,bd_noswap
    ld h,b
    ld l,c
    ld b,d
    ld c,e
    ex de,hl
bd_noswap:
    ld (lp_ch_vx),bc            ; x at the top row
    ex de,hl
    or a
    sbc hl,bc
    ld (lp_ch_dx),hl            ; dx
    ld de,(bd_dy)
    ld a,d
    or a
    jr nz,bd_div
    ld a,e
    cp 1
    jr z,bd_dy1
    cp 2
    jr z,bd_dy2
    push hl
    ld hl,_l2p_recip
    add hl,de
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    pop hl
    call l_z80n_muls_32_16x16   ; DEHL = slope 16.16
    jr bd_slope
bd_dy1:
    ex de,hl                    ; slope = dx << 16
    ld hl,0
    jr bd_slope
bd_dy2:                         ; dx << 15
    ld a,h
    sra a
    ld d,a
    ld a,l
    rra
    ld e,a
    ld hl,0
    rr h
    jr bd_slope
bd_div:
    ld de,(bd_dy)
    push de
    push hl
    call _l2p_slope_div
    pop af
    pop af
bd_slope:
    ; store bits 8..31 as the slope
    ld bc,(bd_dst)              ; slope at +3..+5
    push bc
    inc bc
    inc bc
    inc bc
    ld a,h
    ld (bc),a
    inc bc
    ld a,e
    ld (bc),a
    inc bc
    ld a,d
    ld (bc),a
    pop bc
    ; x at the first row drawn
    ld hl,(lp_ycur)
    ld de,(bd_ytop)
    or a
    sbc hl,de                   ; t
    jr z,bd_at_top
    push hl
    ld hl,(bd_dy)
    push hl
    ld hl,(lp_ch_dx)
    push hl
    ld hl,(lp_ch_vx)
    push hl
    call _l2p_x_at              ; DEHL, 16.16
    pop af
    pop af
    pop af
    pop af
    jr bd_setx
bd_at_top:
    ld de,(lp_ch_vx)
    ld hl,0x8000
bd_setx:
    ld bc,(bd_dst)
    ld a,h
    ld (bc),a
    inc bc
    ld a,e
    ld (bc),a
    inc bc
    ld a,d
    ld (bc),a                   ; x at +0..+2
    ld hl,(bd_dst)
    ld de,6
    add hl,de
    ld (bd_dst),hl
    ld hl,bd_left
    dec (hl)
    jp nz,bd_rail
    ; the spans: (left rail, right rail, colour) -> (&x left, &x right, colour)
    ld l,(ix+13)
    ld h,(ix+14)
    ld a,(ix+15)
    or a
    jp z,bd_done
    ld (bd_ns),a
    ld b,a
    ld de,bd_sp
bd_span_prep:
    ld a,(hl)
    call bd_xptr
    ld a,(hl)
    call bd_xptr
    ld a,(hl)
    ld (de),a
    inc hl
    inc de
    djnz bd_span_prep
    ; the rows
bd_row:
    ; the row's page and address
    ld a,(lp_ycur)
    ld b,a
    rlca
    rlca
    rlca
    and 7
    ld hl,l2p_base
    add a,(hl)
    ld hl,l2p_page
    cp (hl)
    jr z,bd_mapped
    ld (hl),a
    nextreg 0x53,a
bd_mapped:
    ld a,b
    and 31
    or 0x60
    ld (bd_rowhi),a
    push ix
    ld ix,bd_sp
    ld a,(bd_ns)
    ld (bd_left),a
bd_span:
    ld l,(ix+0)
    ld h,(ix+1)
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = x of the left rail
    ld l,(ix+2)
    ld h,(ix+3)
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; HL = x of the right rail
    ; left <= right (a rail pair can cross near the eye)
    ld a,h
    xor d
    jp p,bs_same
    bit 7,h
    jr z,bs_ordered
    ex de,hl
    jr bs_ordered
bs_same:
    ld a,l
    sub e
    ld a,h
    sbc a,d
    jr nc,bs_ordered
    ex de,hl
bs_ordered:
    ; clip to 0..255 (bands ignore l2p_xmin / l2p_xmax)
    bit 7,h
    jr nz,bs_next               ; right < 0
    ld a,h
    or a
    jr z,bs_r_ok
    ld l,255
bs_r_ok:
    bit 7,d
    jr nz,bs_l0                 ; left < 0
    ld a,d
    or a
    jr nz,bs_next               ; left > 255
    jr bs_l_ok
bs_l0:
    ld e,0
bs_l_ok:
    ld a,l
    sub e
    jr c,bs_next
    inc a                       ; length, 0 = 256
    ld c,a
    ld a,(bd_rowhi)
    ld d,a                      ; DE = where
    ld a,(ix+4)
    ld (l2p_col),a
    ld a,c
    or a
    jr z,bs_dma
    cp SPAN_DMA
    jr nc,bs_dma
    ; unrolled
    ld hl,lf_unroll_end
    add a,a
    neg
    add a,l
    ld l,a
    jr c,bs_go
    dec h
bs_go:
    ld a,(l2p_col)
    call bs_jp
    jr bs_next
bs_dma:
    ld (dma_dst),de
    ld b,0
    or a
    jr nz,bs_len
    inc b
bs_len:
    ld (dma_len),bc
    ld hl,dma_prog
    ld bc,(DMA_PROG_LEN << 8) | 0x6B
    otir
bs_next:
    ld de,5
    add ix,de
    ld hl,bd_left
    dec (hl)
    jp nz,bd_span
    pop ix
    ; next row
    ld hl,(lp_ycur)
    inc hl
    ld (lp_ycur),hl
    ld de,(lp_ystop)
    or a
    sbc hl,de
    jr z,bd_done
    ; every rail: x += slope (24 bits)
    ld a,(bd_nr)
    ld b,a
    ld hl,bd_rx
bd_step:
    ld d,h
    ld e,l
    inc de
    inc de
    inc de                      ; DE = slope
    ld a,(de)
    add a,(hl)
    ld (hl),a
    inc hl
    inc de
    ld a,(de)
    adc a,(hl)
    ld (hl),a
    inc hl
    inc de
    ld a,(de)
    adc a,(hl)
    ld (hl),a
    inc hl
    inc hl
    inc hl
    inc hl
    djnz bd_step
    jp bd_row
bd_done:
    pop ix
    ret
bs_jp:
    jp (hl)

; DE -> the next span entry: write &bd_rx[A].x integer part (bd_rx + 6 * A + 1); HL++
bd_xptr:
    push hl
    push de
    ld e,a
    ld d,6
    mul d,e
    ld hl,bd_rx + 1
    add hl,de
    ex de,hl
    pop hl
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ex de,hl                    ; DE = the next free byte
    pop hl
    inc hl
    ret

; void l2p_rect(int16_t y0, int16_t y1, int16_t x0, int16_t x1, uint8_t colour)
_l2p_rect:
    ld hl,10
    add hl,sp
    ld a,(hl)
    ld (l2p_col),a
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; y0
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; y1
    inc hl
    push bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld (bd_rx),bc               ; x0
    ld c,(hl)
    inc hl
    ld b,(hl)
    ld (bd_rx + 2),bc           ; x1
    pop hl                      ; HL = y1
    ; y0 = max(y0, top), y1 = min(y1, bot)
    push hl
    ex de,hl
    ld a,(_l2p_top)
    ld e,a
    ld d,0
    call cmp_hl_de
    jr nc,rc_y0
    ex de,hl
rc_y0:
    ld (lp_ycur),hl
    pop hl
    ld a,(_l2p_bot)
    ld e,a
    ld d,0
    call cmp_hl_de
    jr c,rc_y1
    ex de,hl
rc_y1:
    ld (lp_ystop),hl
    ; whole rows (x0 <= 0, x1 >= 255, no x clipping): one DMA a page
    ld a,(_l2p_xmin)
    or a
    jr nz,rc_row
    ld a,(_l2p_xmax)
    inc a
    jr nz,rc_row
    ld hl,(bd_rx)
    dec hl
    bit 7,h
    jr z,rc_row                 ; x0 > 0
    ld hl,(bd_rx + 2)
    ld de,255
    call cmp_hl_de
    jr c,rc_row                 ; x1 < 255
rc_page:
    ld hl,(lp_ystop)
    ld de,(lp_ycur)
    call cmp_hl_de
    ret c
    ret z
    ld a,e                      ; y
    or 31
    inc a                       ; the next page's first row (0 = 256)
    ld c,a
    ld a,(lp_ystop)
    dec a
    cp c
    jr c,rc_last                ; ystop - 1 < next page row: stop there
    ld a,c
    jr rc_n
rc_last:
    ld a,(lp_ystop)
rc_n:
    sub e
    ld b,a                      ; rows in this page
    ld a,e
    add a,b
    ld (lp_ycur),a              ; (high byte stays 0)
    ; map the page
    ld a,e
    rlca
    rlca
    rlca
    and 7
    ld hl,l2p_base
    add a,(hl)
    ld hl,l2p_page
    ld (hl),a
    nextreg 0x53,a
    ld a,e
    and 31
    or 0x60
    ld d,a
    ld e,0                      ; DE = row start
    ld c,0                      ; BC = rows * 256
    ld (dma_dst),de
    ld (dma_len),bc
    ld hl,dma_prog
    ld bc,(DMA_PROG_LEN << 8) | 0x6B
    otir
    jr rc_page
rc_row:
    ld hl,(lp_ystop)
    ld de,(lp_ycur)
    call cmp_hl_de
    ret c
    ret z
    ld a,e
    ld b,a
    ld de,(bd_rx)
    ld hl,(bd_rx + 2)
    call l2p_fill
    ld hl,(lp_ycur)
    inc hl
    ld (lp_ycur),hl
    jr rc_row

; map the page of row A, set l2p_rowhi (keeps BC, DE, HL)
row_map:
    push hl
    ld h,a
    rlca
    rlca
    rlca
    and 7
    ld l,a
    ld a,(l2p_base)
    add a,l
    ld l,a
    ld a,(l2p_page)
    cp l
    jr z,rm_same
    ld a,l
    ld (l2p_page),a
    nextreg 0x53,a
rm_same:
    ld a,h
    and 31
    or 0x60
    ld (l2p_rowhi),a
    pop hl
    ret

; the row after lp_ycur starts a new page (keeps BC, DE, HL)
pp_nextpage:
    ld a,(lp_ycur)
    inc a
    jr row_map

; fill row l2p_rowhi from DE to HL (int16, inclusive, either order), l2p_col;
; clipped to 0..255 and l2p_xmin..l2p_xmax. Uses A, BC, DE, HL.
ffill:
    ld a,d
    or h
    jr nz,ff_clip               ; an end outside 0..255
ff_in:
    ld a,(_l2p_xmin)
    cp e
    jr c,ff_l
    ld e,a                      ; x0 <= xmin: xmin
ff_l:
    ld a,(_l2p_xmax)
    cp l
    jr nc,ff_r
    ld l,a                      ; x1 > xmax: xmax
ff_r:
    ld a,l
    sub e
    ret c
    inc a
    ld c,a
    ld a,(l2p_rowhi)
    ld d,a
    ld a,c
    or a
    jp z,lf_dma
    cp SPAN_DMA
    jp nc,lf_dma
    ld hl,lf_unroll_end
    add a,a
    neg
    add a,l
    ld l,a
    jr c,ff_go
    dec h
ff_go:
    ld a,(l2p_col)
    jp (hl)
ff_clip:
    bit 7,h
    ret nz                      ; x1 < 0
    ld a,h
    or a
    jr z,ff_c1
    ld l,255                    ; x1 > 255
ff_c1:
    bit 7,d
    jr nz,ff_c0                 ; x0 < 0
    ld a,d
    or a
    ret nz                      ; x0 > 255
    jr ff_in
ff_c0:
    ld e,0
    jr ff_in

; chains <-> registers: A in HL, C, DE, B; B in the other set; the rows they
; end on, clamped to 255, in pp_vars + 1, + 2
ld_chains:
    ld hl,(cha_yend)
    ld a,h
    or a
    ld a,l
    jr z,ldc_a
    ld a,255
ldc_a:
    ld (pp_vars + 1),a
    ld hl,(chb_yend)
    ld a,h
    or a
    ld a,l
    jr z,ldc_b
    ld a,255
ldc_b:
    ld (pp_vars + 2),a
    ld a,(chb_x + 1)
    ld c,a
    ld hl,(chb_x + 2)
    ld a,(chb_dx + 1)
    ld b,a
    ld de,(chb_dx + 2)
    exx
    ld a,(cha_x + 1)
    ld c,a
    ld hl,(cha_x + 2)
    ld a,(cha_dx + 1)
    ld b,a
    ld de,(cha_dx + 2)
    ret

st_chains:
    ld a,c
    ld (cha_x + 1),a
    ld (cha_x + 2),hl
    exx
    ld a,c
    ld (chb_x + 1),a
    ld (chb_x + 2),hl
    exx
    ret

; chain_next: IX = chain (i, x[4], dx[4], yend[2]), A = step (+1 / -1).
; Moves to the first edge from vertex i that ends below row lp_ycur and sets
; x to the edge's x at row lp_ycur and dx to its slope (16.16). Gives up after
; n edges (then yend = lp_ycur + big, dx = 0).
CH_I    equ 0
CH_X    equ 1
CH_DX   equ 5
CH_YEND equ 9

chain_next:
    ld (lp_ch_step),a
    ld a,(lp_pn)
    ld (lp_ch_left),a
cn_loop:
    ld a,(lp_ch_left)
    or a
    jp z,cn_stuck
    dec a
    ld (lp_ch_left),a
    ; v = i, w = i + step (mod n)
    ld a,(ix+CH_I)
    ld c,a
    ld a,(lp_ch_step)
    add a,c
    cp 0xFF
    jr nz,cn_nwrap
    ld a,(lp_pn)
    dec a
cn_nwrap:
    ld hl,lp_pn
    cp (hl)
    jr c,cn_ok
    xor a
cn_ok:
    ld (ix+CH_I),a              ; w becomes the current vertex
    ; pointers: v (C), w (A)
    call vptr
    ld (lp_pw),hl
    ld a,c
    call vptr
    ld (lp_pvv),hl
    ; yend = w.y; skip edges that end at or above lp_ycur
    ld hl,(lp_pw)
    inc hl
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (ix+CH_YEND),e
    ld (ix+CH_YEND+1),d
    ld hl,(lp_ycur)
    ex de,hl                    ; HL = yend, DE = lp_ycur
    call cmp_hl_de
    jr c,cn_loop
    jr z,cn_loop
    ; dy = w.y - v.y (> 0 here, as the edge ends below lp_ycur >= v.y... or v below: then
    ; it is going up and the polygon is not convex at this point: treat as dy >= 1)
    ld hl,(lp_pvv)
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = v.x
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = v.y
    ld (lp_ch_vy),de
    ld (lp_ch_vx),bc
    ld l,(ix+CH_YEND)
    ld h,(ix+CH_YEND+1)
    or a
    sbc hl,de
    ld (lp_ch_dy),hl               ; dy
    ld hl,(lp_pw)
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    or a
    sbc hl,bc
    ld (lp_ch_dx),hl               ; dx = w.x - v.x
    ; slope = (dx << 16) / dy
    ld de,(lp_ch_dy)
    ld a,d
    or a
    jr nz,cn_div
    ld a,e
    cp 1
    jr z,cn_dy1
    cp 2
    jr z,cn_dy2
    ; dx * recip[dy]
    push hl
    ld hl,_l2p_recip
    add hl,de
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    pop hl
    call l_z80n_muls_32_16x16   ; DEHL
    jr cn_slope
cn_dy1:
    ex de,hl                    ; DE = dx (the high word)
    ld hl,0
    jr cn_slope
cn_dy2:                         ; dx << 15
    ld a,h
    sra a
    ld d,a
    ld a,l
    rra
    ld e,a                      ; DE = dx >> 1 (arithmetic)
    ld hl,0
    rr h                        ; the bit shifted out -> bit 15 of the low word
    jr cn_slope
cn_div:
    ld de,(lp_ch_dy)
    push de
    push hl
    call _l2p_slope_div         ; DEHL
    pop af
    pop af
cn_slope:
    ld (ix+CH_DX),l
    ld (ix+CH_DX+1),h
    ld (ix+CH_DX+2),e
    ld (ix+CH_DX+3),d
    ; x at lp_ycur: v.x + 0.5 if the edge starts at or below lp_ycur, else interpolated
    ld hl,(lp_ycur)
    ld de,(lp_ch_vy)
    or a
    sbc hl,de                   ; t = lp_ycur - v.y
    jr z,cn_at_v
    bit 7,h
    jr nz,cn_at_v               ; edge starts below lp_ycur (not convex): from v
    push hl                     ; t
    ld hl,(lp_ch_dy)
    push hl
    ld hl,(lp_ch_dx)
    push hl
    ld hl,(lp_ch_vx)
    push hl
    call _l2p_x_at              ; DEHL = x at lp_ycur, 16.16
    pop af
    pop af
    pop af
    pop af
    jr cn_setx
cn_at_v:
    ld de,(lp_ch_vx)
    ld hl,0x8000                ; + 0.5
cn_setx:
    ld (ix+CH_X),l
    ld (ix+CH_X+1),h
    ld (ix+CH_X+2),e
    ld (ix+CH_X+3),d
    ret
cn_stuck:
    xor a
    ld (ix+CH_DX),a
    ld (ix+CH_DX+1),a
    ld (ix+CH_DX+2),a
    ld (ix+CH_DX+3),a
    ld (ix+CH_YEND),0xFF
    ld (ix+CH_YEND+1),0x7F
    ret

; HL = &xy[A * 2] (vertex A); keeps BC
vptr:
    ld l,a
    ld h,0
    add hl,hl
    add hl,hl
    ld de,(lp_pv)
    add hl,de
    ret

; signed compare HL with DE: carry set if HL < DE, Z if equal. Keeps HL, DE.
cmp_hl_de:
    push hl
    or a
    sbc hl,de
    jr z,cmp_eq
    ld a,h
    pop hl
    jp po,cmp_nov
    xor 0x80                    ; overflow: the sign is the other way
cmp_nov:
    rla                         ; carry = sign of HL - DE
    ret                         ; (NZ: A was not 0 as HL != DE... see below)
cmp_eq:
    pop hl
    ret                         ; Z, NC

; ---------------------------------------------------------------- the unrolled fill

; DE = destination, A = colour: stores from DE upwards; entered 2 * n bytes
; before lf_unroll_end to store n bytes (n < SPAN_DMA).
lf_unroll:
    REPT SPAN_DMA
    ld (de),a
    inc e
    ENDR
lf_unroll_end:
    ret


SECTION data_user

; the zxnDMA (see sdk/src/next/dma.c): set up once by l2p_begin ...
dma_init:
    defb 0x83                   ; WR6: disable
    defb 0x7D                   ; WR0: A -> B, A address + length follow
    defw l2p_col                ;      A: the colour byte
    defw 1
    defb 0x64, 0x02             ; WR1: A memory, fixed; 2-cycle timing
    defb 0x50, 0x02             ; WR2: B memory, incrementing; 2-cycle timing
    defb 0x82                   ; WR5: stop at the end
dma_init_end:
DMA_INIT_LEN equ dma_init_end - dma_init
; ... then each span: its length and where
dma_prog:
    defb 0x65                   ; WR0: A -> B, length follows
dma_len:
    defw 0
    defb 0xAD                   ; WR4: burst, B address follows
dma_dst:
    defw 0
    defb 0xCF                   ; WR6: load
    defb 0x87                   ; WR6: enable
dma_prog_end:
DMA_PROG_LEN equ dma_prog_end - dma_prog

_l2p_top:  defb 0
_l2p_bot:  defb 192
_l2p_xmin: defb 0
_l2p_xmax: defb 255

SECTION bss_user

l2p_saved: defs 1
l2p_base:  defs 1
l2p_page:  defs 1
l2p_col:   defs 1
lp_pv:        defs 2
lp_pn:        defs 1
lp_itop:      defs 1
lp_ymin:      defs 2
lp_ymax:      defs 2
lp_ycur:      defs 2
lp_ystop:     defs 2
lp_pw:        defs 2
lp_pvv:       defs 2
lp_ch_step:   defs 1
lp_ch_left:   defs 1
lp_ch_vx:     defs 2
lp_ch_vy:     defs 2
lp_ch_dx:     defs 2
lp_ch_dy:     defs 2
bd_swap:   defs 1
bd_ytop:   defs 2
bd_ybot:   defs 2
bd_dy:     defs 2
bd_src:    defs 2
bd_src2:   defs 2
bd_dst:    defs 2
bd_nr:     defs 1
bd_ns:     defs 1
bd_left:   defs 1
bd_rowhi:  defs 1
l2p_rowhi: defs 1
bd_rx:     defs 6 * 32          ; L2P_RAILS rails: x (3 bytes), slope (3 bytes)
bd_sp:     defs 5 * 32          ; L2P_SPANS spans
cha:
cha_i:     defs 1
cha_x:     defs 4
cha_dx:    defs 4
cha_yend:  defs 2
chb:
chb_i:     defs 1
chb_x:     defs 4
chb_dx:    defs 4
chb_yend:  defs 2
pp_vars:   defs 3               ; ystop, chain A's and B's next vertex rows (8 bits)
