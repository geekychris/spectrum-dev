; tile16(src, dst): a 16 x 16 tile, column-major (16 bytes a column), into
; Layer 2's 320 mode at dst (a column's address in a mapped page: the next
; column is 256 bytes on). ~4,600 T-states.
;
; void tile16(const uint8_t *src, uint8_t *dst)  (callee)

SECTION code_user
PUBLIC _tile16

_tile16:
    pop bc                      ; return address
    pop de                      ; dst
    pop hl                      ; src
    push bc
    ld b,16
col:
    push bc
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
    ld a,e                      ; back up 16 rows, on to the next column
    sub 16
    ld e,a
    inc d
    pop bc
    djnz col
    ret
