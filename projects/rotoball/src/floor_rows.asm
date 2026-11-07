; The floor's copper moves (view.c prepare_copper), in asm: zsdcc's loop
; took ~900 T-states a row.
;
; uint16_t floor_rows(uint8_t *out, const int16_t *zc, int16_t cx,
;                     uint16_t n, uint16_t neg, uint16_t line)
;
; For n floor rows: the row's cell along the pitch is bit 8 of
; cx + zc[i] (neg: cx - zc[i]); where it differs from the row above (and
; at the first row) write a copper WAIT for `line` + i at h 40 (the
; horizontal blank before the row) and a MOVE of NextReg 0x43 = parity * 4
; (the second Layer 2 palette on odd rows). Lines stay below 256.
; Returns the bytes written (4 per change).

SECTION code_user

PUBLIC _floor_rows

_floor_rows:
    push ix
    ld ix,0
    add ix,sp                   ; +4 out, +6 zc, +8 cx, +10 n, +12 neg, +14 line
    ld a,(ix+12)
    ld (neg),a
    ld a,(ix+14)
    ld (line),a
    ld e,(ix+4)
    ld d,(ix+5)                 ; DE = out
    ld l,(ix+6)
    ld h,(ix+7)                 ; HL = zc
    ld b,(ix+10)                ; B = rows
    ld c,2                      ; C = the last parity (none yet)
row:
    push bc
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = zc[i]
    inc hl
    push hl
    ld l,(ix+8)
    ld h,(ix+9)                 ; HL = cx
    ld a,(neg)
    or a
    jr nz,minus
    add hl,bc
    jr parity
minus:
    sbc hl,bc                   ; (carry is clear after OR)
parity:
    ld a,h
    and 1
    pop hl
    pop bc
    cp c
    jr z,next
    ld c,a                      ; a change: WAIT line,40 ; MOVE 0x43,parity*4
    add a,a
    add a,a
    push af
    ld a,0xD0                   ; WAIT: 0x80 | 40 << 1 | line bit 8 (0)
    ld (de),a
    inc de
    ld a,(line)
    ld (de),a
    inc de
    ld a,0x43
    ld (de),a
    inc de
    pop af
    ld (de),a
    inc de
next:
    ld a,(line)
    inc a
    ld (line),a
    djnz row
    ex de,hl
    ld e,(ix+4)
    ld d,(ix+5)
    or a
    sbc hl,de                   ; bytes written
    pop ix
    ret

SECTION bss_user
neg:  defb 0
line: defb 0
