; dots48.h: XOR n points (x, y byte pairs) onto the screen through zx_row.

SECTION code_user

PUBLIC _dots_xor
EXTERN _zx_row

; void dots_xor(const uint8_t *xy, uint8_t n)
_dots_xor:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = list
    inc hl
    ld b,(hl)                   ; B = n
    ld a,b
    or a
    ret z
    ex de,hl                    ; HL = list
dots_loop:
    ld c,(hl)                   ; C = x
    inc hl
    ld a,(hl)                   ; A = y
    inc hl
    push hl
    ld l,a
    ld h,0
    add hl,hl
    ld de,_zx_row
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = row (32-byte aligned on the screen)
    ld a,c
    rrca
    rrca
    rrca
    and 31
    or e
    ld e,a
    ld a,c
    and 7
    ld hl,dots_bits
    add a,l
    ld l,a
    jr nc,dots_nc
    inc h
dots_nc:
    ld a,(de)
    xor (hl)
    ld (de),a
    pop hl
    djnz dots_loop
    ret

dots_bits:
    defb 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 0x02, 0x01
