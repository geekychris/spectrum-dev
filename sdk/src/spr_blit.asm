; Inner loop of the 48K sprite blitter (see gfx48.c spr_op): applies
; spr_n bytes per line, spr_lines lines, from spr_src (advancing spr_stride
; per line) to the screen at column spr_col of lines spr_y.., with
; spr_mode 0 = XOR, 1 = OR, 2 = AND NOT, 3 = store. Parameters in globals
; (set by C).

SECTION code_user

PUBLIC _spr_blit
PUBLIC _spr_src, _spr_col, _spr_y, _spr_n, _spr_lines, _spr_stride, _spr_mode
EXTERN _zx_row

_spr_blit:
    push ix
    ld a,(_spr_lines)
    or a
    jr z,blit_done
    ld b,a                      ; B = lines left
    ld hl,(_spr_src)
    ld a,(_spr_y)
    ld c,a                      ; C = y
blit_line:
    push bc
    push hl
    ; DE = zx_row[y] + col
    ld l,c
    ld h,0
    add hl,hl
    ld de,_zx_row
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld a,(_spr_col)
    add a,e
    ld e,a
    jr nc,blit_nc
    inc d
blit_nc:
    pop hl                      ; HL = source line
    push hl
    ld a,(_spr_n)
    ld b,a
    ld a,(_spr_mode)
    or a
    jr z,blit_xor
    dec a
    jr z,blit_or
    dec a
    jr z,blit_andn
blit_store:
    ld a,(hl)
    ld (de),a
    inc hl
    inc de
    djnz blit_store
    jr blit_next
blit_andn:
    ld a,(hl)
    cpl
    ex de,hl
    and (hl)
    ld (hl),a
    ex de,hl
    inc hl
    inc de
    djnz blit_andn
    jr blit_next
blit_or:
    ld a,(de)
    or (hl)
    ld (de),a
    inc hl
    inc de
    djnz blit_or
    jr blit_next
blit_xor:
    ld a,(de)
    xor (hl)
    ld (de),a
    inc hl
    inc de
    djnz blit_xor
blit_next:
    pop hl
    ld a,(_spr_stride)
    add a,l
    ld l,a
    jr nc,blit_nc2
    inc h
blit_nc2:
    pop bc
    inc c                       ; next screen line
    djnz blit_line
blit_done:
    pop ix
    ret

SECTION bss_user
_spr_src:    defw 0
_spr_col:    defb 0
_spr_y:      defb 0
_spr_n:      defb 0
_spr_lines:  defb 0
_spr_stride: defb 0
_spr_mode:   defb 0
