; l2w_blit_rle (see l2rle.h): a column-RLE piece into the 320 x 256 Layer 2,
; through a colour table or straight. ~56 T-states a pixel with a table,
; 21 without (LDIR).
;
; void l2w_blit_rle(int16_t x, int16_t y, const uint8_t *piece, const uint8_t *lut)

SECTION code_user

PUBLIC _l2w_blit_rle
EXTERN _l2w_bank
EXTERN _l2w_slot
EXTERN _l2w_restore

LUTBUF equ 0xF200

_l2w_blit_rle:
    push ix
    ld ix,0
    add ix,sp                   ; ix+4 x, ix+6 y, ix+8 piece, ix+10 lut
    ld a,(_l2w_slot)            ; drawing through MMU slot l2w_slot:
    add a,0x50                  ; its register and its address's high byte
    ld (mmu_reg),a
    ld a,(_l2w_slot)
    rrca
    rrca
    rrca
    ld (slot_hi),a

    ld a,(ix+6)
    ld (cur_y),a
    ld l,(ix+4)
    ld h,(ix+5)
    ld (cur_x),hl

    ; the colour table, if any, to LUTBUF (so H = 0xF2 indexes it)
    ld l,(ix+10)
    ld h,(ix+11)
    ld a,h
    or l
    ld (use_lut),a
    jr z,no_lut
    ld de,LUTBUF
    ld bc,256
    ldir
no_lut:
    ld l,(ix+8)
    ld h,(ix+9)
    ld a,(hl)                   ; w
    ld (cols_left),a
    inc hl
    inc hl                      ; skip h
    ld a,(cols_left)
    or a
    jp z,done

col_loop:
    ; is the column on the screen? 0 <= x < 320
    ld de,(cur_x)
    bit 7,d
    jr nz,col_skip
    ld a,d
    or a
    jr z,col_on
    dec a
    jr nz,col_skip
    ld a,e
    cp 64
    jr nc,col_skip
col_on:
    ; page = bank * 2 + x / 32; column address (slot << 13) + (x & 31) * 256 + y
    ld a,e
    rlca
    rlca
    rlca
    and 7
    bit 0,d
    jr z,col_lo
    add a,8
col_lo:
    ld c,a
    ld a,(_l2w_bank)
    add a,a
    add a,c
    push bc
    ld bc,0x243B
    push af
    ld a,(mmu_reg)
    out (c),a
    inc b
    pop af
    out (c),a
    pop bc
    ld a,(slot_hi)
    ld d,a
    ld a,e
    and 31
    add a,d
    ld d,a
    ld a,(cur_y)
    ld e,a                      ; DE = destination

run_loop:
    ld a,(hl)                   ; skip, or 0xFF = end of column
    inc hl
    cp 0xFF
    jr z,col_next
    add a,e
    ld e,a
    ld b,(hl)                   ; len
    inc hl
    ld a,(use_lut)
    or a
    jr nz,run_lut
    ld c,b                      ; straight copy
    ld b,0
    ldir
    jr run_loop
run_lut:
    exx
    ld h,LUTBUF / 256
    exx
lut_px:
    ld a,(hl)
    inc hl
    exx
    ld l,a
    ld a,(hl)
    exx
    ld (de),a
    inc e
    djnz lut_px
    jr run_loop

col_skip:                       ; walk over the column's data
    ld a,(hl)
    inc hl
    cp 0xFF
    jr z,col_next
    ld c,(hl)                   ; len
    inc hl
    ld b,0
    add hl,bc
    jr col_skip

col_next:
    ld de,(cur_x)
    inc de
    ld (cur_x),de
    ld a,(cols_left)
    dec a
    ld (cols_left),a
    jp nz,col_loop

done:
    ld bc,0x243B                ; put page l2w_restore back
    ld a,(mmu_reg)
    out (c),a
    inc b
    ld a,(_l2w_restore)
    out (c),a
    pop ix
    ret

SECTION bss_user
mmu_reg:   defb 0
slot_hi:   defb 0
use_lut:   defb 0
cols_left: defb 0
cur_y:     defb 0
cur_x:     defw 0
