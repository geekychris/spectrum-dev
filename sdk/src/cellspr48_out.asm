; cellspr48 (see cellspr48.h): compose one character cell and write it.
;
; In (set by cellspr48.c):
;   _cs_tilep  the cell's tile, 8 bytes        _cs_attr   its attribute
;   _cs_dst    screen address of its 1st line  _cs_attrp  attribute address
;   _cs_cx     the cell's column in the room   _cs_cy     its first pixel line
;   _cs_act    _cs_nact sprites, 9 bytes each: c0, c1 (byte columns covered),
;              y, h, stride (w+1), gfx (the shifted copy, 2 bytes), keep, ink
; The tile goes to _cs_buf, each sprite over the cell is ORed in (line by
; line, from its pre-shifted copy), and its colour applied:
; attr = (attr & keep) | ink. Then 8 lines to the screen (+256 each) and
; the attribute.

SECTION code_user

defc CS_MAX_DIRTY = 200             ; cellspr48.h

PUBLIC _cs_compose, _cs_flush, _cs_mark_rc
EXTERN _cs_list_c, _cs_list_r, _cs_list_n, _cs_dirty, _cs_map_row, _cs_tiles, _cs_attrs
EXTERN _cs_col, _cs_row, _cs_w, _cs_h, _cs_overflow
PUBLIC _cs_tilep, _cs_attr, _cs_dst, _cs_attrp, _cs_cx, _cs_cy, _cs_nact, _cs_act, _cs_buf

; void cs_mark_rc(uint16_t rc) __z88dk_fastcall: HL = row << 8 | column.
; Adds the cell to the dirty list unless it is there (its bit in cs_dirty) or
; off the room; a full list sets cs_overflow (cs_update then redraws all).
_cs_mark_rc:
    ld a,(_cs_w)
    dec a
    cp l
    ret c                           ; column >= w
    ld a,(_cs_h)
    dec a
    cp h
    ret c                           ; row >= h
    ld c,l
    ld b,h
    ld a,h                          ; dirty + (row << 2 | column >> 3)
    add a,a
    add a,a
    ld e,a
    ld a,l
    rrca
    rrca
    rrca
    and 0x1F
    or e
    ld e,a
    ld d,0
    ld hl,_cs_dirty
    add hl,de
    ld a,c
    and 7
    ld e,a
    ld a,1
    inc e
cs_mk_bit:
    dec e
    jr z,cs_mk_bit_done
    rlca
    jr cs_mk_bit
cs_mk_bit_done:
    ld e,a
    and (hl)
    ret nz                          ; already in the list
    ld a,(_cs_list_n)
    cp CS_MAX_DIRTY
    jr nz,cs_mk_add
    ld a,1
    ld (_cs_overflow),a
    ret
cs_mk_add:
    ld a,e
    or (hl)
    ld (hl),a
    ld a,(_cs_list_n)
    ld e,a
    ld d,0
    ld hl,_cs_list_c
    add hl,de
    ld (hl),c
    ld hl,_cs_list_r
    add hl,de
    ld (hl),b
    inc a
    ld (_cs_list_n),a
    ret

; void cs_flush(void): compose every cell in the dirty list, clearing its bit
_cs_flush:
    ld a,(_cs_list_n)
    or a
    ret z
    ld hl,0
    ld (cs_i),hl                    ; cs_i = 0
cs_fl:
    ld hl,(cs_i)
    ld de,_cs_list_c
    add hl,de
    ld c,(hl)                       ; C = column
    ld hl,(cs_i)
    ld de,_cs_list_r
    add hl,de
    ld b,(hl)                       ; B = row
    ; dirty[(r << 2) | (c >> 3)] &= ~(1 << (c & 7))
    ld a,b
    add a,a
    add a,a
    ld e,a
    ld a,c
    rrca
    rrca
    rrca
    and 0x1F
    or e
    ld e,a
    ld d,0
    ld hl,_cs_dirty
    add hl,de
    ld a,c
    and 7
    ld e,a
    ld a,0xFE
    inc e
cs_fl_bit:
    dec e
    jr z,cs_fl_bit_done
    rlca
    jr cs_fl_bit
cs_fl_bit_done:
    and (hl)
    ld (hl),a
    ; id = map_row[r][c]
    ld a,c
    ld (_cs_cx),a
    ld a,b
    add a,a
    add a,a
    add a,a
    ld (_cs_cy),a
    ld l,b
    ld h,0
    add hl,hl
    ld de,_cs_map_row
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ex de,hl
    ld e,c
    ld d,0
    add hl,de
    ld e,(hl)                       ; E = tile id
    ld hl,(_cs_attrs)
    add hl,de
    ld a,(hl)
    ld (_cs_attr),a
    ex de,hl                        ; HL = id
    add hl,hl
    add hl,hl
    add hl,hl
    ld de,(_cs_tiles)
    add hl,de
    ld (_cs_tilep),hl
    ; screen cell (c + cs_col, r + cs_row)
    ld a,(_cs_col)
    add a,c
    ld c,a
    ld a,(_cs_row)
    add a,b
    ld b,a
    and 7
    rrca
    rrca
    rrca
    or c
    ld l,a                          ; (row & 7) << 5 | col
    ld a,b
    and 0x18
    or 0x40
    ld h,a
    ld (_cs_dst),hl
    ld a,b
    rrca
    rrca
    rrca
    and 3
    or 0x58
    ld h,a
    ld (_cs_attrp),hl
    call _cs_compose
    ld hl,(cs_i)
    inc hl
    ld (cs_i),hl
    ld a,(_cs_list_n)
    cp l
    jp nz,cs_fl
    xor a
    ld (_cs_list_n),a
    ret

_cs_compose:
    push ix                         ; sdcc_ix's frame pointer
    ld hl,(_cs_tilep)               ; the tile -> buf
    ld de,_cs_buf
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ld a,(_cs_nact)
    or a
    jp z,cs_write
    ld ix,_cs_act
    ld b,a
cs_spr:
    push bc
    ld a,(_cs_cx)                   ; c0 <= cx <= c1 ?
    sub (ix+0)
    jp c,cs_next
    ld c,a                          ; C = j, the byte column in the sprite
    ld a,(ix+1)
    ld hl,_cs_cx
    cp (hl)
    jp c,cs_next
    ; lines: the sprite covers y .. y+h-1, the cell cy .. cy+7
    ld a,(ix+2)                     ; y
    add a,(ix+3)                    ; y + h
    ld hl,_cs_cy
    cp (hl)
    jp c,cs_next                    ; y + h < cy
    jp z,cs_next                    ; y + h = cy
    ld a,(_cs_cy)
    sub (ix+2)                      ; cy - y
    jr c,cs_below                   ; the sprite starts inside the cell
    ; top = cy - y lines of the sprite are above the cell; buf from line 0
    ld e,a                          ; E = top
    ld a,(ix+3)
    sub e                           ; h - top (> 0)
    cp 8
    jr c,cs_n1
    ld a,8
cs_n1:
    ld d,a                          ; D = lines
    ld hl,_cs_buf
    jr cs_src
cs_below:
    neg                             ; y - cy
    cp 8
    jp nc,cs_next                   ; starts below the cell
    ld e,a
    ld hl,_cs_buf
    add a,l
    ld l,a
    ld a,h
    adc a,0
    ld h,a                          ; HL = buf + (y - cy)
    ld a,8
    sub e                           ; room left in the cell
    cp (ix+3)
    jr c,cs_n2
    ld a,(ix+3)                     ; the whole sprite fits
cs_n2:
    ld d,a
    ld e,0                          ; top = 0
cs_src:
    ; DE' = gfx + j + top * stride
    push hl
    ld l,(ix+5)
    ld h,(ix+6)
    ld a,c
    add a,l
    ld l,a
    jr nc,cs_s1
    inc h
cs_s1:
    ld a,e
    or a
    jr z,cs_s3
    ld c,(ix+4)
    ld b,0
cs_s2:
    add hl,bc
    dec e
    jr nz,cs_s2
cs_s3:
    ex de,hl                        ; DE = source, H = lines (from D)
    ld a,h
    pop hl                          ; HL = buf position
    ld c,(ix+4)                     ; stride
    ld b,a                          ; B = lines
cs_or:
    ld a,(de)
    or (hl)
    ld (hl),a
    inc hl
    ld a,e
    add a,c
    ld e,a
    jr nc,cs_or1
    inc d
cs_or1:
    djnz cs_or
    ld a,(_cs_attr)                 ; attr = (attr & keep) | ink
    and (ix+7)
    or (ix+8)
    ld (_cs_attr),a
cs_next:
    ld de,9
    add ix,de
    pop bc
    dec b
    jp nz,cs_spr
cs_write:
    ld hl,(_cs_dst)
    ld de,_cs_buf
    ld b,8
cs_out_loop:
    ld a,(de)
    ld (hl),a
    inc de
    inc h
    djnz cs_out_loop
    ld hl,(_cs_attrp)
    ld a,(_cs_attr)
    ld (hl),a
    pop ix
    ret

SECTION bss_user
_cs_tilep: defs 2
_cs_dst:   defs 2
_cs_attrp: defs 2
_cs_attr:  defs 1
_cs_cx:    defs 1
_cs_cy:    defs 1
_cs_nact:  defs 1
_cs_buf:   defs 8
_cs_act:   defs 9 * 8
cs_i:      defs 2
