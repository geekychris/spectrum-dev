; Sky Knights: colour for moving objects. A copy of the playfield's
; attributes (cells_bg) is kept; an object paints its cells with its colour
; only where the playfield has the plain sky colour there (cells_plain), and
; the cells are restored from the copy when it moves on. main.c restores
; every object's cells and then paints them all, so overlaps come out right
; (the last painted wins).
;
;   cells_capture()   copy the attributes after drawing the playfield
;   cells_restore()   cl_w x cl_h cells from offset cl_off (row * 32 + col)
;   cells_paint()     same area: plain cells get cl_attr

SECTION code_user

DEFC _cells_bg = 0x7100        ; MEM_CELLS_BG (mem.h)

PUBLIC _cells_capture, _cells_restore, _cells_paint
PUBLIC _cells_plain, _cl_off, _cl_w, _cl_h, _cl_attr

_cells_capture:
    ld hl,0x5800
    ld de,_cells_bg
    ld bc,768
    ldir
    ret

; HL = cells_bg + cl_off, DE = 0x5800 + cl_off, B = cl_h (0: Z set)
cells_setup:
    ld de,(_cl_off)
    ld hl,0x5800
    add hl,de
    ex de,hl
    ld bc,_cells_bg
    add hl,bc
    ld a,(_cl_w)
    or a
    ret z                       ; nothing to do (and ldir with BC = 0 copies 64K)
    ld a,(_cl_h)
    ld b,a
    or a
    ret

_cells_restore:
    call cells_setup
    ret z
cr_row:
    push bc
    push de
    push hl
    ld a,(_cl_w)
    ld c,a
    ld b,0
    ldir
    pop hl
    pop de
    ld bc,32
    add hl,bc
    ex de,hl
    add hl,bc
    ex de,hl
    pop bc
    djnz cr_row
    ret

_cells_paint:
    call cells_setup
    ret z
cp_row:
    push bc
    push de
    push hl
    ld a,(_cl_w)
    ld b,a
    ld a,(_cells_plain)
    ld c,a
cp_cell:
    ld a,(hl)
    cp c
    jr nz,cp_skip
    ld a,(_cl_attr)
    ld (de),a
cp_skip:
    inc hl
    inc de
    djnz cp_cell
    pop hl
    pop de
    ld bc,32
    add hl,bc
    ex de,hl
    add hl,bc
    ex de,hl
    pop bc
    djnz cp_row
    ret

SECTION bss_user
_cells_plain: defb 0
_cl_off:      defw 0
_cl_w:        defb 0
_cl_h:        defb 0
_cl_attr:     defb 0

; ---------------------------------------------------------------- input
; uint8_t isr_scan(void): the 8 keyboard half-rows into in2_rows and the
; Kempston into in2_kemp[0] (as input2.c's in2_scan does, ~10x faster);
; returns 1 if anything changed since the last scan
SECTION code_user
PUBLIC _isr_scan
EXTERN _in2_rows, _in2_kemp, _in2_has_kemp

_isr_scan:
    ld hl,_in2_rows
    ld bc,0xFEFE
    ld e,0                      ; E = changed
is_row:
    in a,(c)
    and 0x1F                    ; the keys (not EAR, which the beeper moves)
    cp (hl)
    jr z,is_same
    ld (hl),a
    ld e,1
is_same:
    inc hl
    rlc b
    jr c,is_row
    ld a,(_in2_has_kemp)
    or a
    jr z,is_done
    ld bc,0x001F
    in a,(c)
    ld d,a
    and 0xE0                    ; bits 5-7 set: nothing there now
    ld a,d
    jr z,is_kemp
    xor a
is_kemp:
    ld hl,_in2_kemp
    cp (hl)
    jr z,is_done
    ld (hl),a
    ld e,1
is_done:
    ld l,e
    ret
