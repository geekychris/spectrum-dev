; l2pic.h: void l2pic_rle(uint8_t *dst, const uint8_t *src, uint8_t rows)
; Runs of (count, colour) from src down one Layer 2 column (dst: 256-byte
; aligned column + the first row) until `rows` are written.

SECTION code_user
PUBLIC _l2pic_rle

_l2pic_rle:
    ld hl,6
    add hl,sp
    ld c,(hl)                   ; C = rows left
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = dst
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; HL = src
rl_run:
    ld a,c
    sub (hl)                    ; rows left - count
    ld c,a
    ld b,(hl)
    inc hl
    ld a,(hl)                   ; the colour
    inc hl
rl_fill:
    ld (de),a
    inc e                       ; a column is 256 bytes: no carry into D
    djnz rl_fill
    ld a,c
    or a
    jr nz,rl_run
    ret
