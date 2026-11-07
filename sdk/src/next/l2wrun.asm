; l2wr_blit (see l2wrun.h): a column-run image (sdk/tools/runimg.py) into
; Layer 2's 320 x 256 mode, each pixel base + its offset, optionally
; mirrored. A run is a jump into an unrolled copy (28 T-states a pixel);
; the next column is the next 256 bytes, and the 8K page changes only every
; 32 columns.
;
; void l2wr_blit(const uint8_t *img, int16_t x, uint8_t mirror, uint8_t base)

SECTION code_user

PUBLIC _l2wr_blit
EXTERN _l2w_bank
EXTERN _l2w_slot
EXTERN _l2w_restore

MAX_RUN equ 25                  ; runimg.py's MAX_RUN

_l2wr_blit:
    push ix
    ld ix,0
    add ix,sp                   ; ix+4 img, ix+6 x, ix+8 mirror, ix+9 base
    ld a,(_l2w_slot)
    add a,0x50
    ld (mmu_reg),a
    ld a,(_l2w_slot)
    rrca
    rrca
    rrca
    ld (slot_hi),a

    ld e,(ix+4)
    ld d,(ix+5)                 ; DE = the image
    ld a,(de)                   ; x0
    ld c,a
    inc de
    ld a,(de)                   ; w
    inc de
    or a
    jp z,done_noslot
    ld (cols_left),a
    ld l,(ix+6)
    ld h,(ix+7)                 ; HL = x
    ld b,0
    ld a,(ix+8)
    or a
    jr nz,mirrored
    add hl,bc                   ; x + x0
    ld a,0x24                   ; inc h
    jr have_x
mirrored:
    or a
    sbc hl,bc                   ; x - x0
    ld a,0x25                   ; dec h
have_x:
    ld (step_op),a
    ; the page of column HL (0..319): bank * 2 + x / 32
    ld a,l
    rlca
    rlca
    rlca
    and 7
    bit 0,h
    jr z,first_lo
    add a,8
first_lo:
    ld b,a
    ld a,(_l2w_bank)
    add a,a
    add a,b
    ld (page),a
    call set_page
    ld a,l
    and 31
    ld b,a
    ld a,(slot_hi)
    add a,b
    ld h,a                      ; H = the column's high byte in the slot
    ld c,(ix+9)                 ; C = base

col_loop:
    ld a,(de)                   ; a run's first row, or 0xFF: the column's end
    inc de
    cp 0xFF
    jp z,col_next
    ld l,a
    ld a,(de)                   ; len (1..MAX_RUN)
    inc de
    ld b,a
    add a,a
    add a,a
    add a,b
    neg
    add a,MAX_RUN * 5           ; skip MAX_RUN - len of the five-byte copies
    ld (run_jr+1),a
run_jr:
    jr run_jr+2
    REPT MAX_RUN
    ld a,(de)
    add a,c
    ld (hl),a
    inc de
    inc l
    ENDR
    jp col_loop

col_next:
    ld a,(cols_left)
    dec a
    jr z,blit_done
    ld (cols_left),a
step_op:
    inc h                       ; inc h / dec h: the next column
    ld a,h
    and 31
    jr z,page_up                ; went from column 31 to 32 of a page
    cp 31
    jp nz,col_loop
    ld a,(step_op)              ; 31: from column 0 to -1 if going left
    cp 0x25
    jp nz,col_loop
    ld a,(page)
    dec a
    ld (page),a
    call set_page
    ld a,(slot_hi)
    add a,31
    ld h,a
    jp col_loop
page_up:
    ld a,(step_op)
    cp 0x24
    jp nz,col_loop
    ld a,(page)
    inc a
    ld (page),a
    call set_page
    ld a,(slot_hi)
    ld h,a
    jp col_loop

blit_done:
    ld a,(_l2w_restore)         ; put page l2w_restore back
    ld (page),a
    call set_page
done_noslot:
    pop ix
    ret

; MMU slot l2w_slot = (page)
set_page:
    push bc
    ld bc,0x243B
    ld a,(mmu_reg)
    out (c),a
    inc b
    ld a,(page)
    out (c),a
    pop bc
    ret

SECTION bss_user
mmu_reg:   defb 0
slot_hi:   defb 0
cols_left: defb 0
page:      defb 0
