; l2poly.asm - convex polygons and spans on the Layer 2 back buffer (l2poly.h).
;
; The back buffer is the 16K bank l2_back (nextl2.c): 256 x 192, 32 rows to
; an 8K page, mapped into MMU slot 3 (0x6000) as the rows need it.

SECTION code_user

PUBLIC _l2p_begin, _l2p_end, _l2p_poly, _l2p_span
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
    ld hl,l2p_col
    ld (dma_src),hl
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
pp_row:
    ; span between the chains
    ld de,(cha_x+2)
    ld hl,(chb_x+2)
    call cmp_hl_de
    jr nc,pp_ordered
    ex de,hl
pp_ordered:
    ld a,(lp_ycur)
    ld b,a
    call l2p_fill
    ; step
    ld hl,(cha_x)
    ld de,(cha_dx)
    add hl,de
    ld (cha_x),hl
    ld hl,(cha_x+2)
    ld de,(cha_dx+2)
    adc hl,de
    ld (cha_x+2),hl
    ld hl,(chb_x)
    ld de,(chb_dx)
    add hl,de
    ld (chb_x),hl
    ld hl,(chb_x+2)
    ld de,(chb_dx+2)
    adc hl,de
    ld (chb_x+2),hl
    ld hl,(lp_ycur)
    inc hl
    ld (lp_ycur),hl
    ld de,(lp_ystop)
    or a
    sbc hl,de
    jr z,pp_done
    ; a chain at the end of its edge moves on
    ld hl,(lp_ycur)
    ld de,(cha_yend)
    or a
    sbc hl,de
    jr nz,pp_b
    ld ix,cha
    ld a,1
    call chain_next
pp_b:
    ld hl,(lp_ycur)
    ld de,(chb_yend)
    or a
    sbc hl,de
    jr nz,pp_row
    ld ix,chb
    ld a,-1
    call chain_next
    jr pp_row
pp_done:
    pop ix
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
; before lf_unroll_end to store n bytes.
lf_unroll:
    REPT 255
    ld (de),a
    inc e
    ENDR
lf_unroll_end:
    ret


SECTION data_user

; the zxnDMA's fill program (see sdk/src/next/dma.c)
dma_prog:
    defb 0x83                   ; WR6: disable
    defb 0x7D                   ; WR0: A -> B, A address + length follow
dma_src:
    defw 0
dma_len:
    defw 0
    defb 0x64, 0x02             ; WR1: A memory, fixed; 2-cycle timing
    defb 0x50, 0x02             ; WR2: B memory, incrementing; 2-cycle timing
    defb 0xAD                   ; WR4: burst, B address follows
dma_dst:
    defw 0
    defb 0x82                   ; WR5: stop at the end
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
