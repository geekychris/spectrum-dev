; sprc_xor (sprclip.h): XOR a pre-shifted sprite (spr_preshift's layout)
; at a signed x, clipped at both screen edges and the bottom.
;
;   _sprc_xor        C entry (zsdcc: x, y, pre, w, h on the stack)
;   asm_sprc_xor     HL = x, A = y, DE = pre, B = w (bytes), C = h (lines);
;                    keeps IX and IY
;
; The line loop steps the screen address (inc d, with the character-row and
; third carries) instead of looking each line up; IXL counts the lines.
; Unclipped 3-byte lines (16-pixel sprites) have their own unrolled loop.

SECTION code_user

PUBLIC _sprc_xor, asm_sprc_xor
EXTERN _zx_row

_sprc_xor:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = x
    inc hl
    ld a,(hl)                   ; y
    inc hl
    push de
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = pre
    inc hl
    ld b,(hl)                   ; w
    inc hl
    ld c,(hl)                   ; h
    pop hl                      ; HL = x
    ; fall through

asm_sprc_xor:
    cp 192
    ret nc
    ld (sc_y),a
    ld a,b
    inc a
    ld (sc_stride),a            ; a line of the pre-shifted image: w + 1 bytes
    ld b,a
    ; on screen at all? x 0..255, or x -256..-1 with some bytes right of 0
    ld a,h
    or a
    jr z,sc_x_ok
    inc a
    ret nz                      ; x > 255 or x < -256
sc_x_ok:
    ld a,l
    and 7
    ld (sc_shift),a
    ; col = x >> 3 (signed byte: H is 0 or 0xFF)
    ld a,l
    rrca
    rrca
    rrca
    and 0x1F
    ld (sc_col),a
    ld a,h
    and 0xE0
    jr z,sc_col_pos
    ld a,(sc_col)
    or 0xE0
    ld (sc_col),a
    neg                         ; bytes off the left
    cp b
    ret nc                      ; all of them
sc_col_pos:
    ld (sc_left),a              ; 0 when x >= 0
    ; plane = stride * h (one shift's bytes), pre += (x & 7) * plane
    push de
    ld e,c
    ld d,0
    ld hl,0
sc_mul:
    add hl,de
    djnz sc_mul
    ex de,hl                    ; DE = plane
    pop hl                      ; HL = pre
    ld a,c
    ld (sc_h),a
    ld a,(sc_shift)
    jr sc_shift_test
sc_shift_add:
    add hl,de
    dec a
sc_shift_test:
    or a
    jr nz,sc_shift_add
    ; clip the left
    ld a,(sc_left)
    or a
    jr z,sc_noleft
    ld e,a
    ld d,0
    add hl,de                   ; skip the bytes off the left
    ld (_sprc_skip),a           ; skipped again on every line
    ld b,a
    ld a,(sc_stride)
    sub b
    ld (_sprc_n),a
    xor a
    ld (sc_col),a
    jr sc_cols_done
sc_noleft:
    ld a,(sc_col)               ; clip the right: n = min(stride, 32 - col)
    ld b,a
    ld a,32
    sub b
    ld c,a
    ld a,(sc_stride)
    cp c
    jr c,sc_n_ok
    ld a,c
sc_n_ok:
    ld (_sprc_n),a
    ld b,a
    ld a,(sc_stride)
    sub b
    ld (_sprc_skip),a
sc_cols_done:
    ld (_sprc_src),hl
    ; screen address of line y, column col
    ld a,(sc_y)
    ld l,a
    ld h,0
    add hl,hl
    ld de,_zx_row
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld a,(sc_col)
    add a,e
    ld e,a                      ; a screen line never crosses a 256 boundary
    ld (_sprc_scr),de
    ; lines = min(h, 192 - y)
    ld a,(sc_y)
    ld b,a
    ld a,192
    sub b
    ld b,a
    ld a,(sc_h)
    cp b
    jr c,sc_lines_ok
    ld a,b
sc_lines_ok:
    ld (_sprc_lines),a
    ; fall through to the line loop

sprc_blit:
    ld a,(_sprc_lines)
    or a
    ret z
    push ix
    ld ixl,a                    ; IXL = lines
    ld a,(_sprc_n)
    ld c,a                      ; C = bytes a line
    ld hl,(_sprc_src)
    ld de,(_sprc_scr)
    ld a,(_sprc_skip)
    or a
    jr nz,sprc_line
    ld a,c
    cp 3
    jr z,sprc_line3

sprc_line:                      ; any width, any skip
    ld b,c
sprc_byte:
    ld a,(de)
    xor (hl)
    ld (de),a
    inc hl
    inc e                       ; stays in the line: col + n <= 32
    djnz sprc_byte
    ld a,e
    sub c
    ld e,a
    ld a,(_sprc_skip)           ; to the next source line
    add a,l
    ld l,a
    jr nc,sprc_nc
    inc h
sprc_nc:
    inc d                       ; next screen line
    ld a,d
    and 7
    jr nz,sprc_next
    ld a,e                      ; next character row
    add a,32
    ld e,a
    jr c,sprc_next              ; ... in the next third
    ld a,d
    sub 8
    ld d,a
sprc_next:
    dec ixl
    jr nz,sprc_line
    pop ix
    ret

sprc_line3:                     ; 3 bytes, no skip
    ld a,(de)
    xor (hl)
    ld (de),a
    inc hl
    inc e
    ld a,(de)
    xor (hl)
    ld (de),a
    inc hl
    inc e
    ld a,(de)
    xor (hl)
    ld (de),a
    inc hl
    dec e
    dec e
    inc d
    ld a,d
    and 7
    jr nz,sprc_next3
    ld a,e
    add a,32
    ld e,a
    jr c,sprc_next3
    ld a,d
    sub 8
    ld d,a
sprc_next3:
    dec ixl
    jr nz,sprc_line3
    pop ix
    ret

SECTION bss_user
_sprc_src:   defw 0
_sprc_scr:   defw 0
_sprc_n:     defb 0
_sprc_lines: defb 0
_sprc_skip:  defb 0
sc_y:        defb 0
sc_h:        defb 0
sc_stride:   defb 0
sc_col:      defb 0
sc_left:     defb 0
sc_shift:    defb 0
