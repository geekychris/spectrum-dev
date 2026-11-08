; render.asm - Boing Twist's texture rows and rooms (the ball itself is
; drawn by the SDK's texview.asm)

SECTION code_user

; void bt_unpack4(const uint8_t *src, uint8_t *dst)
; a row of a room: 128 bytes of two 4-bit pixels (the left one high) -> 256
; pixels; colour i is pen ((i & 12) << 4) | 28 | (i & 3): the entries the
; picture leaves free (28-31 of every other block of 32; the blocks between
; hold the shadowed copies, pen | 0x20)
PUBLIC _bt_unpack4
_bt_unpack4:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = src
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; HL = dst
    ld b,128
up_loop:
    ld a,(de)
    inc de
    ld c,a
    rrca
    rrca
    rrca
    rrca
    call pen
    ld (hl),a
    inc hl
    ld a,c
    call pen
    ld (hl),a
    inc hl
    djnz up_loop
    ret
pen:                            ; low nibble of A -> its pen
    push bc
    ld b,a
    and 12
    rlca
    rlca
    rlca
    rlca
    or 28
    ld c,a
    ld a,b
    and 3
    or c
    pop bc
    ret


; void bt_row(uint8_t *dst, const uint8_t *prog, uint8_t n, uint8_t dv)
; a row of the texture from a band's slots: n records (5 bytes: source low
; byte, source row at dv 0, bytes to copy, the slot's width, turned) laid
; side by side from dst. Row dv of the band: the source row is the record's
; + dv (upright) or - dv and read backwards (turned: the piece upside down).
; A slot one column wider than its piece repeats the last column; each
; slot's first column is the groove (colour 25).
PUBLIC _bt_row
_bt_row:
    push ix
    ld hl,4
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = dst
    inc hl
    ld a,(hl)
    inc hl
    push af
    ld a,(hl)
    inc hl
    ld ixh,a
    pop af
    ld ixl,a                    ; IX = prog
    ld a,(hl)                   ; n
    ld (row_n),a
    inc hl
    ld c,(hl)                   ; C = dv
rr_slot:
    push de                     ; the slot's first column
    ld a,(ix+1)
    bit 0,(ix+4)
    jr nz,rr_turned
    add a,c
    ld h,a
    ld l,(ix+0)
    push bc
    ld c,(ix+2)
    ld b,0
    ldir
    pop bc
    dec hl
    ld a,(hl)
    jr rr_pad
rr_turned:
    sub c
    ld h,a
    ld l,(ix+0)
    push bc
    ld b,(ix+2)
rr_back:
    ld a,(hl)
    dec hl
    ld (de),a
    inc de
    djnz rr_back
    pop bc
    inc hl
    ld a,(hl)
rr_pad:
    ld l,a
    ld a,(ix+3)
    sub (ix+2)
    jr z,rr_nopad
    ld a,l
    ld (de),a
    inc de
rr_nopad:
    pop hl
    ld (hl),25                  ; the groove (C_GROOVE)
    ld a,ixl
    add a,5
    ld ixl,a
    jr nc,rr_nc
    inc ixh
rr_nc:
    ld hl,row_n
    dec (hl)
    jr nz,rr_slot
    pop ix
    ret

SECTION data_user
row_n: defb 0
SECTION code_user

