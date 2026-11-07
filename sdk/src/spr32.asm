; zxgame.h spr32_shift: copy a 32-pixel-wide 1-bit image (4 bytes a line)
; shifted right by 0..7 pixels (bits pushed out of the last byte are lost:
; keep the image's right 7 columns empty). ~40 T-states a line + 36 per bit.
; With spr_put/spr_xor-style blitting (spr_blit), this turns one rendered
; image into any horizontal position without pre-shifting 8 copies.

SECTION code_user

PUBLIC _spr32_shift

; void spr32_shift(uint8_t *dst, const uint8_t *src, uint8_t lines, uint8_t shift)
_spr32_shift:
    push ix
    ld ix,4
    add ix,sp
    ld e,(ix+0)
    ld d,(ix+1)                 ; DE = dst
    ld l,(ix+2)
    ld h,(ix+3)                 ; HL = src
    ld b,(ix+4)                 ; lines
    ld a,(ix+5)
    and 7
    ld c,a                      ; shift
    ld a,b
    or a
    jr z,s32_done
s32_line:
    push bc
    ld a,c
    or a
    jr nz,s32_shifted
    ldi                         ; no shift: copy 4 bytes (BC is saved)
    ldi
    ldi
    ldi
    jr s32_next
s32_shifted:
    push de
    ld d,(hl)
    inc hl
    ld e,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld a,(hl)
    inc hl
    push hl
    ld h,b
    ld l,a                      ; DEHL = the line
    ld b,c
s32_bit:
    srl d
    rr e
    rr h
    rr l
    djnz s32_bit
    ld a,l
    ld b,h
    pop hl                      ; source
    ex (sp),hl                  ; HL = dst, source on the stack
    ld (hl),d
    inc hl
    ld (hl),e
    inc hl
    ld (hl),b
    inc hl
    ld (hl),a
    inc hl
    ex de,hl                    ; DE = dst
    pop hl                      ; HL = source
s32_next:
    pop bc
    djnz s32_line
s32_done:
    pop ix
    ret
