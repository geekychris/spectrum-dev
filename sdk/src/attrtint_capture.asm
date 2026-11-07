; attrtint.h tint_capture: tint_map bit = the cell of 0x5800 holds tint_plain
; (~45,000 T-states; ~180,000 in C).

SECTION code_user

PUBLIC _tint_capture
EXTERN _tint_map, _tint_plain

_tint_capture:
    ld hl,0x5800
    ld de,_tint_map
    ld a,(_tint_plain)
    ld c,a
    ld b,96
tc_byte:
    push bc
    ld b,8
    xor a
    ld (de),a
tc_bit:
    ld a,(hl)
    cp c                        ; Z: plain
    ld a,(de)
    jr nz,tc_no
    scf
    jr tc_rot
tc_no:
    or a
tc_rot:
    rla
    ld (de),a
    inc hl
    djnz tc_bit
    inc de
    pop bc
    djnz tc_byte
    ret
