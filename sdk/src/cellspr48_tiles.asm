; cellspr48.h: void cs_put_tile(uint8_t c, uint8_t r, uint8_t id) - set a cell
; of the room's map and draw its tile straight to the screen (8 lines and the
; attribute, no composing: ~250 T-states against ~600). For many cells at
; once when no sprite is over them (cs_wipe_sprites first): a view that jumps.

SECTION code_user

PUBLIC _cs_put_tile
EXTERN _cs_map_row, _cs_row, _cs_col, _cs_tiles, _cs_attrs, _zx_row

_cs_put_tile:
    ld hl,2
    add hl,sp
    ld c,(hl)                       ; c
    inc hl
    ld b,(hl)                       ; r
    inc hl
    ld e,(hl)                       ; id
    ld a,b                          ; map_row[r][c] = id
    add a,a
    ld l,a
    ld h,0
    push de
    ld de,_cs_map_row
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld e,c
    ld d,0
    add hl,de
    pop de
    ld (hl),e
    ld a,(_cs_row)                  ; the screen row
    add a,b
    ld b,a
    ld a,(_cs_col)
    add a,c
    ld c,a                          ; C = the screen column
    ld d,0                          ; the attribute: 0x5800 + row * 32 + column
    ld l,b
    ld h,d
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld a,l
    or c
    ld l,a
    ld a,h
    or 0x58
    ld h,a
    push hl
    ld hl,(_cs_attrs)
    add hl,de
    ld a,(hl)
    pop hl
    ld (hl),a
    ld l,e                          ; DE = the tile's bytes
    ld h,d
    add hl,hl
    add hl,hl
    add hl,hl
    ld de,(_cs_tiles)
    add hl,de
    ex de,hl
    ld a,b                          ; HL = the cell's first line
    add a,a
    add a,a
    add a,a
    ld l,a
    ld h,0
    add hl,hl
    push de
    ld de,_zx_row
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    add a,c
    ld l,a
    pop de
    ld a,(de)
    ld (hl),a
    inc de
    inc h
    ld a,(de)
    ld (hl),a
    inc de
    inc h
    ld a,(de)
    ld (hl),a
    inc de
    inc h
    ld a,(de)
    ld (hl),a
    inc de
    inc h
    ld a,(de)
    ld (hl),a
    inc de
    inc h
    ld a,(de)
    ld (hl),a
    inc de
    inc h
    ld a,(de)
    ld (hl),a
    inc de
    inc h
    ld a,(de)
    ld (hl),a
    ret
