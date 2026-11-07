; plat.h: uint8_t plat_kind_at(int16_t px, int16_t py) - the tile id under a
; pixel, plat_outside off the map. In asm because it is called a dozen times
; a step (zsdcc's variable shifts are a call per bit).

SECTION code_user

PUBLIC _plat_kind_at
EXTERN _plat_shift, _plat_outside, _tmap_w, _tmap_h, _tmap_row

_plat_kind_at:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                       ; DE = px
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                          ; HL = py
    bit 7,d
    jr nz,pk_out
    bit 7,h
    jr nz,pk_out
    ld a,(_plat_shift)
    or a
    jr z,pk_shifted
    ld b,a
pk_shift:
    srl d
    rr e
    srl h
    rr l
    djnz pk_shift
pk_shifted:
    ld a,d                          ; column >= 256 or >= w: off the map
    or a
    jr nz,pk_out
    ld a,h
    or a
    jr nz,pk_out
    ld a,(_tmap_w)
    dec a
    cp e
    jr c,pk_out
    ld a,(_tmap_h)
    dec a
    cp l
    jr c,pk_out
    add hl,hl                       ; tmap_row[row]
    ld bc,_tmap_row
    add hl,bc
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld d,0
    add hl,de
    ld l,(hl)
    ret
pk_out:
    ld a,(_plat_outside)
    ld l,a
    ret
