; l2rle.c's span core: n pixels of one row, p = (p & and) | or, going right
; one column (+256) at a time, all in the mapped 8K page.
;
; void l2w_span_core(uint8_t *p, uint16_t n, uint16_t and_or)   (and: low byte)

SECTION code_user

PUBLIC _l2w_span_core

_l2w_span_core:
    push ix
    ld ix,0
    add ix,sp                   ; ix+4 p, ix+6 n, ix+8 and, ix+9 or
    ld l,(ix+4)
    ld h,(ix+5)
    ld b,(ix+6)
    ld c,(ix+8)
    ld e,(ix+9)
spx:
    ld a,(hl)
    and c
    or e
    ld (hl),a
    inc h
    djnz spx
    pop ix
    ret
