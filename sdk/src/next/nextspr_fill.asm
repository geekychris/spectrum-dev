; nextspr.c's mask: make transparent every pixel of a sprite group image
; that a column-RLE piece covers (see nextspr.h, sprg_mask_rle). The C side
; checks that the piece meets the group at all.
;
; void sprg_mask_core(uint8_t *img, uint16_t cols, uint16_t rows,
;                     int16_t px, int16_t py, const uint8_t *piece)
;
; The image is cols x rows 16x16 patterns, pattern after pattern: going down
; a pixel is +16, from a pattern's row 15 to the next pattern row is
; +16 + (cols - 1) * 256, and pattern row k starts at k * cols * 256.

SECTION code_user

PUBLIC _sprg_mask_core

_sprg_mask_core:
    push ix
    ld ix,0
    add ix,sp                   ; ix+4 img, +6 cols, +8 rows, +10 px, +12 py, +14 piece
    ld a,(ix+6)
    ld (band_hi),a              ; a pattern row: cols * 256
    dec a
    ld (rowstep_hi),a           ; row 15 -> the next pattern row: (cols - 1) * 256 + 16
    ld a,(ix+6)
    add a,a
    add a,a
    add a,a
    add a,a
    ld (gw),a
    ld a,(ix+8)
    add a,a
    add a,a
    add a,a
    add a,a
    ld (gh),a
    ld l,(ix+14)
    ld h,(ix+15)
    ld a,(hl)                   ; w
    ld (wleft),a
    inc hl
    inc hl
    ld e,(ix+10)
    ld d,(ix+11)
    ld (curx),de

col_loop:
    ld de,(curx)                ; the column in the group? 0 <= x < gw
    xor a
    ld (colok),a
    ld a,d
    or a
    jr nz,col_start
    ld a,(gw)
    ld b,a
    ld a,e
    cp b
    jr nc,col_start
    and 15                      ; colptr = img + (x >> 4) * 256 + (x & 15)
    ld c,a
    ld a,e
    rrca
    rrca
    rrca
    rrca
    and 15
    ld b,a
    push hl
    ld l,(ix+4)
    ld h,(ix+5)
    add hl,bc
    ld (colptr),hl
    pop hl
    ld a,1
    ld (colok),a
col_start:
    ld e,(ix+12)
    ld d,(ix+13)
    ld (cury),de

run_loop:
    ld a,(hl)                   ; skip, or 0xFF: the column's end
    inc hl
    cp 0xFF
    jp z,col_next
    ld de,(cury)                ; y += skip
    add a,e
    ld e,a
    ld a,d
    adc a,0
    ld d,a
    ld (cury),de
    ld a,(hl)                   ; len
    inc hl
    ld (curlen),a
    ld b,a
    ld a,(colok)
    or a
    jr z,run_skip
    push hl
    ld h,d                      ; HL = y + len
    ld l,e
    ld c,b
    ld b,0
    add hl,bc
    bit 7,h                     ; y1 = min(y + len, gh), nothing if <= 0
    jr nz,run_none
    ld a,h
    or a
    jr nz,y1_gh
    ld a,(gh)
    cp l
    jr c,y1_gh
    ld a,l
    jr y1_set
y1_gh:
    ld a,(gh)
y1_set:
    ld c,a
    bit 7,d                     ; y0 = max(y, 0)
    jr nz,y0_zero
    ld a,d
    or a
    jr nz,run_none
    ld a,e
    jr y0_set
y0_zero:
    xor a
y0_set:
    cp c
    jr nc,run_none              ; y0 >= y1: nothing to do
    ld b,a                      ; y0
    ld a,c
    sub b
    ld c,a                      ; n = y1 - y0
    ld hl,(colptr)              ; q = colptr + (y0 & 15) * 16 + (y0 >> 4) * band
    ld a,b
    and 15
    add a,a
    add a,a
    add a,a
    add a,a
    add a,l
    ld l,a
    ld a,h
    adc a,0
    ld h,a
    ld a,b
    rrca
    rrca
    rrca
    rrca
    and 15
    jr z,q_done
    ld d,a
    ld a,(band_hi)
    ld e,a
q_band:
    ld a,h
    add a,e
    ld h,a
    dec d
    jr nz,q_band
q_done:
    ld a,b                      ; rows left in this pattern: 16 - (y0 & 15)
    and 15
    ld e,a
    ld a,16
    sub e
    ld b,a
fill_chunk:
    ld a,c
    cp b
    jr nc,fill_full
    ld b,a
fill_full:
    ld a,c
    sub b
    ld c,a
fill_px:
    ld (hl),0xE3
    add hl,16                   ; Z80N
    djnz fill_px
    ld a,c
    or a
    jr z,run_none
    ld a,(rowstep_hi)           ; on to the pattern below
    add a,h
    ld h,a
    ld b,16
    jr fill_chunk
run_none:
    pop hl
run_skip:
    ld a,(curlen)               ; past the run's pixels, y += len
    ld c,a
    ld b,0
    add hl,bc
    ld de,(cury)
    ex de,hl
    add hl,bc
    ex de,hl
    ld (cury),de
    jp run_loop

col_next:
    ld de,(curx)
    inc de
    ld (curx),de
    ld a,(wleft)
    dec a
    ld (wleft),a
    jp nz,col_loop
    pop ix
    ret

SECTION bss_user
band_hi:    defb 0
rowstep_hi: defb 0
gw:         defb 0
gh:         defb 0
wleft:      defb 0
colok:      defb 0
curlen:     defb 0
curx:       defw 0
cury:       defw 0
colptr:     defw 0
