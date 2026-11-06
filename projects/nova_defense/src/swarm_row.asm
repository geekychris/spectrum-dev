; Draw one swarm row (see main.c draw_row). The row is aligned to a
; character row, so the next pixel line is D+1 (no table lookups).
;
; row_dest   screen address of the row's first pixel line, column 0
; row_c0/wc  byte columns to clear first (0 width = no clear)
; row_mask   bit c = alien in column c is alive. With row_wc = 0 the row is
;            redrawn without a separate clear; row_offs = 0 means clear only.
; row_x      grid_x (signed 16-bit)
; row_offs   -> 8 signed words: x offset of each column
; row_shift  -> 8 pointers: pre-shifted image (8 lines x 3 bytes) for x & 7
; Aliens with x outside 0..231 are skipped.

SECTION code_user

PUBLIC _swarm_row_draw
PUBLIC _row_dest, _row_c0, _row_wc, _row_mask, _row_x, _row_offs, _row_shift

_swarm_row_draw:
    push ix
    ; ---- clear the span on all 8 lines
    ld a,(_row_wc)
    or a
    jr z,clear_done
    ld hl,(_row_dest)
    ld a,(_row_c0)
    add a,l
    ld l,a                      ; 32 columns per line: never carries
    ld c,8
clear_line:
    ld e,l
    ld a,(_row_wc)
    ld b,a
    xor a
    srl b
    jr nc,clear_pairs
    ld (hl),a
    inc l
clear_pairs:
    jr z,clear_eol
clear_byte:
    ld (hl),a
    inc l
    ld (hl),a
    inc l
    djnz clear_byte
clear_eol:
    ld l,e
    inc h
    dec c
    jr nz,clear_line
clear_done:
    ld hl,(_row_offs)
    ld a,h
    or l
    jp z,row_done               ; row_offs = 0: clear only (a dropped row's old place)
    ld a,(_row_mask)
    ; ---- every column: alive aliens store their 3 bytes per line, dead
    ;      ones store zeros; a first byte shared with the previous alien's
    ;      last byte is ORed. Footprints touch, so this rewrites the whole
    ;      row; the bytes either side are cleared for the step it moved.
    ld c,a                      ; C = alive mask
    ld ix,(_row_offs)
    ld a,0xFF
    ld (last_end),a             ; no previous alien
    ld b,8
column:
    xor a
    srl c                       ; next column's alive bit -> A
    rla
    ld (cur_alive),a
    push bc
    ld hl,(_row_x)
    ld e,(ix+0)
    ld d,(ix+1)
    add hl,de
    bit 7,h
    jp nz,skip                  ; x < 0
    ld a,h
    or a
    jp nz,skip                  ; x > 255
    ld a,l
    cp 240
    jp nc,skip                  ; x >= 240 (byte column > 29)
    ; source: the shifted image if alive, else zeros
    ld de,zero_block
    ld a,(cur_alive)
    or a
    jr z,have_src
    ld a,l
    and 7
    add a,a
    ld e,a
    ld d,0
    push hl
    ld hl,(_row_shift)
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    pop hl
have_src:
    ; column = x >> 3; clear the byte before the first alien
    ld a,l
    srl a
    srl a
    srl a
    ld l,a                      ; L = byte column
    ld a,(last_end)
    cp 0xFF
    jr nz,not_first
    ld a,l
    or a
    jr z,not_first
    dec a
    call clear_col              ; byte left of the swarm
not_first:
    ld a,(last_end)
    cp l
    ld a,0                      ; A = 0 store first byte, 1 = OR it
    jr nz,mode_set
    inc a
mode_set:
    ld (first_or),a
    ld a,l
    add a,2
    ld (last_end),a
    ; HL = destination, DE = source
    ld a,(_row_dest)
    add a,l
    ld l,a
    ld a,(_row_dest+1)
    ld h,a
    ex de,hl                    ; HL = source, DE = destination
    ld a,(first_or)
    or a
    jr z,lines
    ; first byte shared with the previous alien: OR it, line by line
    push de
    push hl
    ld b,8
or_first:
    ld a,(de)
    or (hl)
    ld (de),a
    inc hl
    inc hl
    inc hl
    inc d
    djnz or_first
    pop hl
    pop de
    inc hl                      ; the other two bytes are stored below
    inc e
    ld b,8
or_rest:
    ld a,(hl)
    ld (de),a
    inc hl
    inc e
    ld a,(hl)
    ld (de),a
    inc hl
    inc hl
    dec e
    inc d
    djnz or_rest
    jr column_done
lines:
    ; 8 lines of 3 bytes: LDI x3, then DE += 256 - 3 (next pixel line)
    REPT 8
    ldi
    ldi
    ldi
    ex de,hl
    ld bc,253
    add hl,bc
    ex de,hl
    ENDR
column_done:
    pop bc
    jr next_column
skip:
    pop bc
next_column:
    inc ix
    inc ix
    dec b
    jp nz,column
    ; clear the byte after the last alien
    ld a,(last_end)
    cp 0xFF
    jp z,row_done
    inc a
    cp 32
    jp nc,row_done
    call clear_col
    jp row_done

; clear byte column A on the row's 8 lines (keeps BC, DE, HL, IX)
clear_col:
    push hl
    push bc
    ld hl,(_row_dest)
    add a,l
    ld l,a
    xor a
    ld b,8
clear_col_l:
    ld (hl),a
    inc h
    djnz clear_col_l
    pop bc
    pop hl
    ret

row_done:
    pop ix
    ret

SECTION rodata_user
zero_block: defs 24

SECTION bss_user
last_end:   defb 0
cur_alive:  defb 0
first_or:   defb 0
_row_dest:  defw 0
_row_c0:    defb 0
_row_wc:    defb 0
_row_mask:  defb 0
_row_x:     defw 0
_row_offs:  defw 0
_row_shift: defw 0
