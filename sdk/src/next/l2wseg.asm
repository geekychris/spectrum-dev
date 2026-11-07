; l2ws_blit (see l2wseg.h): a column-segment image (sdk/tools/segimg.py) into
; Layer 2's 320 x 256 mode, each code through l2ws_pen[code], optionally
; mirrored. ~24 T-states a pixel plus ~90 a segment.
;
; void l2ws_blit(const uint8_t *img, int16_t x, uint8_t mirror)

SECTION code_user

PUBLIC _l2ws_blit
PUBLIC _l2ws_pen
EXTERN _l2w_bank
EXTERN _l2w_slot
EXTERN _l2w_restore

_l2ws_blit:
    push ix
    ld ix,0
    add ix,sp                   ; ix+4 img, ix+6 x, ix+8 mirror
    ld a,(_l2w_slot)
    add a,0x50
    ld (mmu_reg),a
    ld a,(_l2w_slot)
    rrca
    rrca
    rrca
    ld (slot_hi),a

    ld l,(ix+4)
    ld h,(ix+5)
    ld c,(hl)                   ; x0
    inc hl
    ld a,(hl)                   ; w
    inc hl
    or a
    jp z,done_noslot
    ld (cols_left),a
    ld e,(ix+6)
    ld d,(ix+7)                 ; DE = x
    ld b,0
    ld a,(ix+8)
    or a
    jr nz,mirrored
    ex de,hl
    add hl,bc                   ; x + x0
    ex de,hl
    ld a,1
    jr have_x
mirrored:
    ex de,hl
    or a
    sbc hl,bc                   ; x - x0
    ex de,hl
    ld a,0xFF
have_x:
    ld (step),a
    ld (cur_x),de
    ld a,(_l2ws_pen+1)
    ld ixl,a
    ld a,(_l2ws_pen+2)
    ld ixh,a

col_loop:
    ; page = bank * 2 + x / 32 (x 0..319); column address (slot << 13) + (x & 31) * 256
    ld de,(cur_x)
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
    ld a,(hl)                   ; ytop, or 0xFF: an empty column
    inc hl
    cp 0xFF
    jr z,col_next
    ld e,a                      ; DE = destination

seg_loop:
    ld a,(hl)
    inc hl
    or a
    jr z,col_next
    ld c,a
    and 63
    ld b,a                      ; len
    ld a,c
    and 0xC0
    jr z,seg_skip
    cp 0x80
    jr z,pen2
    jr nc,pen3
    ld a,ixl
    jr fill
pen2:
    ld a,ixh
    jr fill
pen3:
    ld a,(_l2ws_pen+3)
fill:
    ld (de),a
    inc e
    djnz fill
    jr seg_loop
seg_skip:
    ld a,e
    add a,b
    ld e,a
    jr seg_loop

col_next:
    ld de,(cur_x)
    ld a,(step)
    cp 1
    jr z,col_inc
    dec de
    jr col_stepped
col_inc:
    inc de
col_stepped:
    ld (cur_x),de
    ld a,(cols_left)
    dec a
    ld (cols_left),a
    jp nz,col_loop

    ld bc,0x243B                ; put page l2w_restore back
    ld a,(mmu_reg)
    out (c),a
    inc b
    ld a,(_l2w_restore)
    out (c),a
done_noslot:
    pop ix
    ret

SECTION data_user
_l2ws_pen: defb 0, 1, 2, 3

SECTION bss_user
mmu_reg:   defb 0
slot_hi:   defb 0
cols_left: defb 0
step:      defb 0
cur_x:     defw 0
