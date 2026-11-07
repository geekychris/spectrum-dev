; jq.c: uint8_t tile_px(int16_t px, int16_t py) - the level's tile under a
; pixel, as the original's level_get_tile(px / 16, py / 16): / rounds towards
; zero, so -15..-1 is tile 0; off the map is TILE_EMPTY (0). In asm because
; the enemies and the player look up ~40 tiles a tick (~1,000 T-states each
; in C through plat_kind_at, ~150 here).

SECTION code_user

PUBLIC _tile_px, tile_at
EXTERN _tmap_w, _tmap_row

_tile_px:
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
    call tile_at
    ld l,a
    ret

; in: DE = px, HL = py; out: A = the tile (BC, DE, HL changed)
tile_at:
    bit 7,d                         ; px < 0: -15..-1 is column 0, less is off
    jr z,tp_x_ok
    ld a,e
    cp 0xF1                         ; px >= -15 (DE = 0xFFF1..0xFFFF)
    jr c,tp_out
    inc d
    jr nz,tp_out
    ld e,d                          ; DE = 0
tp_x_ok:
    bit 7,h
    jr z,tp_y_ok
    ld a,l
    cp 0xF1
    jr c,tp_out
    inc h
    jr nz,tp_out
    ld l,h                          ; HL = 0
tp_y_ok:
    ld a,h                          ; row = py / 16 < 14 (py < 224)
    or a
    jr nz,tp_out
    ld a,l
    cp 224
    jr nc,tp_out
    rrca
    rrca
    rrca
    and 0x1E                        ; row * 2: the tmap_row index
    ld l,a
    ld h,0
    ld bc,_tmap_row
    add hl,bc
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                          ; HL = the row
    ld a,d                          ; column = px / 16 < tmap_w (px < 4096)
    and 0xF0
    jr nz,tp_out
    ld a,e
    srl d
    rra
    srl d
    rra
    srl d
    rra
    srl d
    rra                             ; A = column
    ld e,a
    ld a,(_tmap_w)
    dec a
    cp e
    jr c,tp_out
    ld d,0
    add hl,de
    ld a,(hl)
    ret
tp_out:
    xor a
    ret
