; hmap48.h hmap_fill and hmap_line in assembly (in C they took ~1.2 M
; T-states for a screen; this is ~10x faster).

SECTION code_user

PUBLIC _hmap_fill, _hmap_line
EXTERN _zx_row

; void hmap_fill(const uint8_t *h, uint8_t dy, uint8_t bottom, const uint8_t *pattern)
_hmap_fill:
    push ix
    ld ix,4
    add ix,sp
    ld l,(ix+0)
    ld h,(ix+1)
    ld (hf_h),hl
    ld a,(ix+2)
    ld (hf_dy),a
    ld a,(ix+3)
    cp 192
    jr c,hf_bot
    ld a,191
hf_bot:
    ld (hf_bottom),a
    ld l,(ix+4)
    ld h,(ix+5)
    ld (hf_pat),hl
    xor a
    ld (hf_col),a
hf_column:
    ; the 8 starts (h + dy, 255 on overflow) -> hf_s, lo, hi
    ld hl,(hf_h)
    ld de,hf_s
    ld bc,0x08FF                ; B = 8, C = lo
    xor a
    ld (hf_hi),a
hf_starts:
    ld a,(hf_dy)
    add a,(hl)
    jr nc,hf_s_ok
    ld a,255
hf_s_ok:
    ld (de),a
    cp c
    jr nc,hf_not_lo
    ld c,a
hf_not_lo:
    push bc
    ld b,a
    ld a,(hf_hi)
    cp b
    jr nc,hf_not_hi
    ld a,b
    ld (hf_hi),a
hf_not_hi:
    pop bc
    inc hl
    inc de
    djnz hf_starts
    ld (hf_h),hl
    ; rows lo .. bottom
    ld a,(hf_bottom)
    cp c
    jr c,hf_next_col            ; all below the bottom
    sub c
    inc a
    ld b,a                      ; B = rows
    ld a,c                      ; A = y
hf_row:
    push bc
    ld (hf_y),a
    ; mask: columns whose start <= y
    ld c,a
    ld a,(hf_hi)
    cp c
    ld a,0xFF
    jr c,hf_mask_done           ; hi < y: all
    jr z,hf_mask_done           ; hi == y: all
    push hl
    ld hl,hf_s
    ld b,8
    xor a
hf_mask:
    ld e,a
    ld a,c
    cp (hl)                     ; y >= s ?
    ccf
    ld a,e
    rla                         ; carry = (y >= s)
    inc hl
    djnz hf_mask
    pop hl
hf_mask_done:
    ld e,a                      ; E = mask
    ; pattern[y & 7]
    ld a,c
    and 7
    ld hl,(hf_pat)
    add a,l
    ld l,a
    jr nc,hf_pnc
    inc h
hf_pnc:
    ld a,(hl)
    and e
    ld e,a
    ; zx_row[y] + col
    ld l,c
    ld h,0
    add hl,hl
    ld bc,_zx_row
    add hl,bc
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld a,(hf_col)
    or l
    ld l,a
    ld a,e
    or (hl)
    ld (hl),a
    ld a,(hf_y)
    inc a
    pop bc
    djnz hf_row
hf_next_col:
    ld a,(hf_col)
    inc a
    ld (hf_col),a
    cp 32
    jp c,hf_column
    pop ix
    ret

; void hmap_line(const uint8_t *h, uint8_t dy): each column x from y = h[x] + dy
; to the previous column's height (a continuous line), clipped at 191
_hmap_line:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    ld (hl_dy),a
    ex de,hl                    ; HL = h
    ld a,(hl_dy)
    add a,(hl)
    ld (hl_prev),a
    xor a
    ld (hl_x),a
hl_col:
    ld a,(hl_dy)
    add a,(hl)
    ld c,a                      ; C = y
    push hl
    ld a,(hl_prev)
    ld b,a                      ; B = prev
    ld a,c
    ld (hl_prev),a
    ; top / bottom of the run in this column
    ld a,b
    inc a
    cp c
    jr nc,hl_up
    ld d,a                      ; prev + 1 < y: prev+1 .. y
    ld e,c
    jr hl_run
hl_up:
    ld a,c
    inc a
    cp b
    jr nc,hl_one
    ld d,c                      ; y + 1 < prev: y .. prev-1
    ld a,b
    dec a
    ld e,a
    jr hl_run
hl_one:
    ld d,c
    ld e,c
hl_run:
    ld a,e
    cp 192
    jr c,hl_clip
    ld e,191
hl_clip:
    ; mask and column
    ld a,(hl_x)
    and 7
    ld hl,hl_bits
    add a,l
    ld l,a
    jr nc,hl_bnc
    inc h
hl_bnc:
    ld c,(hl)                   ; C = mask
    ld a,(hl_x)
    rrca
    rrca
    rrca
    and 31
    ld b,a                      ; B = column
hl_pix:
    ld a,e
    cp d
    jr c,hl_done                ; top > bottom
    push de
    ld l,d
    ld h,0
    add hl,hl
    ld de,_zx_row
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    or b
    ld l,a
    ld a,c
    or (hl)
    ld (hl),a
    pop de
    inc d
    jr nz,hl_pix
hl_done:
    pop hl
    inc hl
    ld a,(hl_x)
    inc a
    ld (hl_x),a
    jr nz,hl_col
    ret

hl_bits:
    defb 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01

SECTION bss_user

hf_h:      defw 0
hf_pat:    defw 0
hf_dy:     defb 0
hf_bottom: defb 0
hf_col:    defb 0
hf_hi:     defb 0
hf_y:      defb 0
hf_s:      defs 8
hl_dy:     defb 0
hl_prev:   defb 0
hl_x:      defb 0
