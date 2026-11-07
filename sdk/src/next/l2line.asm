; l2line.h: one clipped line on the Layer 2 back buffer (l2_back, nextl2.c),
; Bresenham from the upper end down, so the 8K page (32 rows, at 0x6000)
; only ever moves on. HL = 0x6000 + (y & 31) * 256 + x, C = colour,
; B = pixels left, E = error, IXH = major length, IXL = minor length.

SECTION code_user

PUBLIC _l2l_core, _l2l_x0, _l2l_y0, _l2l_x1, _l2l_y1, _l2l_col
EXTERN _l2_back

_l2l_core:
    push ix
    ld a,0x53
    ld bc,0x243B
    out (c),a
    inc b
    in a,(c)
    ld (ll_saved),a             ; MMU slot 3 now
    ; from the upper end
    ld a,(_l2l_y1)
    ld hl,_l2l_y0
    cp (hl)
    jr nc,ll_ordered
    ld b,(hl)                   ; swap the ends
    ld (hl),a
    ld a,b
    ld (_l2l_y1),a
    ld a,(_l2l_x0)
    ld b,a
    ld a,(_l2l_x1)
    ld (_l2l_x0),a
    ld a,b
    ld (_l2l_x1),a
ll_ordered:
    ld a,(_l2l_y1)
    ld hl,_l2l_y0
    sub (hl)
    ld (ll_ady),a               ; dy >= 0
    ld a,(_l2l_x1)
    ld hl,_l2l_x0
    sub (hl)
    ld b,0x2C                   ; inc l: right
    jr nc,ll_xpos
    neg
    ld b,0x2D                   ; dec l: left
ll_xpos:
    ld (ll_adx),a
    ld a,b
    ld (xm_step),a
    ld (ym_step),a
    ; the page and the address of (x0, y0)
    ld a,(_l2_back)
    add a,a
    ld b,a
    ld a,(_l2l_y0)
    rlca
    rlca
    rlca
    and 7
    add a,b
    ld (ll_page),a
    nextreg 0x53,a
    ld a,(_l2l_y0)
    and 31
    add a,0x60
    ld h,a
    ld a,(_l2l_x0)
    ld l,a
    ld a,(_l2l_col)
    ld c,a
    ld a,(ll_ady)
    ld d,a
    ld a,(ll_adx)
    cp d
    jr c,ll_ymaj
    ; x major: major = adx, minor = ady
    ld ixh,a
    ld ixl,d
    ld b,a
    inc b                       ; pixels (0 = 256)
    srl a
    ld e,a
xm_loop:
    ld (hl),c
xm_step:
    inc l
    ld a,e
    sub ixl
    jr nc,xm_noy
    add a,ixh
    ld e,a
    inc h
    ld a,h
    cp 0x80
    call z,ll_nextpage
    djnz xm_loop
    jr ll_done
xm_noy:
    ld e,a
    djnz xm_loop
    jr ll_done
ll_ymaj:
    ; y major: major = ady (D), minor = adx (A)
    ld ixl,a
    ld a,d
    ld ixh,a
    ld b,a
    inc b
    srl a
    ld e,a
ym_loop:
    ld (hl),c
    inc h
    ld a,h
    cp 0x80
    call z,ll_nextpage
    ld a,e
    sub ixl
    jr nc,ym_nox
    add a,ixh
ym_step:
    inc l
ym_nox:
    ld e,a
    djnz ym_loop
ll_done:
    ld a,(ll_saved)
    nextreg 0x53,a
    pop ix
    ret

; the next 32 rows: the next page at 0x6000
ll_nextpage:
    ld h,0x60
    ld a,(ll_page)
    inc a
    ld (ll_page),a
    nextreg 0x53,a
    ret

SECTION bss_user
_l2l_x0:  defb 0
_l2l_y0:  defb 0
_l2l_x1:  defb 0
_l2l_y1:  defb 0
_l2l_col: defb 0
ll_adx:   defb 0
ll_ady:   defb 0
ll_page:  defb 0
ll_saved: defb 0
