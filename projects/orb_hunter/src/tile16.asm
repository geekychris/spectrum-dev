; tile16(src, dst): a 16 x 16 tile, column-major (16 bytes a column), into
; Layer 2's 320 mode at dst (a column's address in a mapped page: the next
; column is 256 bytes on; all 16 columns in that page). ~4,800 T-states.
;
; void tile16(const uint8_t *src, uint8_t *dst)  (callee)

SECTION code_user
PUBLIC _tile16

_tile16:
    pop bc                      ; return address
    pop hl                      ; src (the first argument is on top)
    pop de                      ; dst
    push bc
    ld b,16
col:
    push bc
    push de                     ; the column's top (rows 240-255 carry into D)
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    pop de
    inc d                       ; the next column
    pop bc
    djnz col
    ret
