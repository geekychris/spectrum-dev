; zxgame.h spr16_shift: a 16-pixel-wide 1-bit image (2 bytes a line) shifted
; right by 0..7 pixels into 3-byte lines: one of spr_preshift's 8 copies, made
; when it is needed (~120 T-states a line + 36 a bit; shifts 5..7 go left from
; the next byte, so 4 bits at most). Sprites that change
; shape and position every frame (cellspr48's) keep 2 x 3 x h bytes this way
; instead of 8 x 3 x h for each frame of animation.

SECTION code_user

PUBLIC _spr16_shift

; void spr16_shift(uint8_t *dst, const uint8_t *src, uint8_t lines, uint8_t shift)
_spr16_shift:
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
    jr z,s16_done
s16_line:
    push bc
    ld a,(hl)
    inc hl
    ld b,(hl)                       ; (B is saved)
    push hl
    ld h,a
    ld a,c
    cp 5
    jr nc,s16_left
    ld a,b                          ; H A L = the line, 0: shift right C times
    ld l,0
    inc c
    jr s16_rtest
s16_rbit:
    srl h
    rra
    rr l
s16_rtest:
    dec c
    jr nz,s16_rbit
    jr s16_store
s16_left:                           ; shifts 5..7: H A L = 0, the line, shifted
    ld a,h                          ; left 8 - C times (3 at most)
    ld l,b
    ld h,0
    ld b,a
    ld a,8
    sub c
    ld c,a
    ld a,b
s16_lbit:
    sla l
    rla
    rl h
    dec c
    jr nz,s16_lbit
s16_store:
    ex de,hl                        ; HL = dst, D E = bytes 0 and 2
    ld (hl),d
    inc hl
    ld (hl),a
    inc hl
    ld (hl),e
    inc hl
    ex de,hl                        ; DE = dst
    pop hl
    inc hl                          ; next source line
    pop bc
    djnz s16_line
s16_done:
    pop ix
    ret
