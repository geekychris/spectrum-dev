; cellspr48_scroll.c: cs_scroll_rows(fill, n, left) - cs_scroll's work on the
; rows (see cellspr48.h): for each row, does it equal itself shifted? (then
; its pixels and colours need not move), the map row moves and takes the new
; cells from fill, the pixel lines and attributes move, and the new cells that
; differ from what the screen shows there are marked.
;
; cs_shift_lines(uint8_t *line, uint8_t n, uint8_t keep,
; uint8_t left) - move `keep` bytes of each of a character row's 8 pixel lines
; (line = its first line's address at the room's first column) n bytes left
; (left = 1) or right (0), with unrolled LDI / LDD (16 T-states a byte; LDIR
; is 21). A line of the room stays inside its 32-byte block, so the offsets
; are 8-bit.

SECTION code_user

PUBLIC _cs_shift_lines, _cs_scroll_rows, _cs_scroll_same
EXTERN _cs_map_row, _cs_w, _cs_h, _cs_row, _cs_col, _zx_row, _cs_mark_rc

_cs_shift_lines:
    push ix
    ld ix,4
    add ix,sp
    ld a,32                         ; run the last `keep` of 32 LDI / LDD
    sub (ix+3)
    add a,a
    ld e,a
    ld d,0
    ld hl,csl_ldi
    ld a,(ix+4)
    or a
    jr nz,csl_dir
    ld hl,csl_ldd
csl_dir:
    add hl,de
    ld (csl_call + 1),hl
    ld a,(ix+4)                     ; offsets from the line's start:
    or a
    ld a,(ix+2)
    ld c,0                          ; left: from n to 0
    jr nz,csl_offs
    ld a,(ix+3)                     ; right: from keep - 1 to keep - 1 + n
    dec a
    ld c,a
    add a,(ix+2)
    ld b,c
    ld c,a
    ld a,b
csl_offs:
    ld (csl_src + 1),a
    ld a,c
    ld (csl_dst + 1),a
    ld l,(ix+0)
    ld h,(ix+1)
    ld b,8
csl_line:
    push bc
    push hl
    ld a,l
csl_dst:
    add a,0                         ; (offset)
    ld e,a
    ld d,h
    ld a,l
csl_src:
    add a,0                         ; (offset)
    ld l,a
csl_call:
    call 0                          ; (into the LDI or LDD run)
    pop hl
    inc h                           ; the next pixel line
    pop bc
    djnz csl_line
    pop ix
    ret

csl_ldi:
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ret
csl_ldd:
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ldd
    ret

; void cs_scroll_rows(const uint8_t *fill, uint8_t n, uint8_t left)
_cs_scroll_rows:
    push ix
    ld ix,4
    add ix,sp
    ld l,(ix+0)
    ld h,(ix+1)
    ld (sr_fill),hl
    ld a,(ix+2)
    ld (sr_n),a
    ld b,a
    ld a,(_cs_w)
    sub b
    ld (sr_keep),a
    ld a,(ix+3)
    ld (sr_left),a
    or a
    ld a,(sr_keep)                  ; the edge: the first new column
    jr nz,sr_edge
    xor a
sr_edge:
    ld (sr_edge0),a
    xor a
    ld (sr_r),a
    ld (sr_total),a
sr_row:
    ld a,(sr_r)                     ; HL = the map row
    add a,a
    ld l,a
    ld h,0
    ld de,_cs_map_row
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld (sr_map),hl
    ld hl,(_cs_scroll_same)         ; the game says this row is unchanged?
    ld a,h
    or l
    jr z,sr_compare
    ld a,(sr_r)
    ld e,a
    ld d,0
    add hl,de
    ld a,(hl)
    or a
    ld c,0
    jr nz,sr_cmp_done               ; (as if every cell matched)
sr_compare:
    ld hl,(sr_map)
    ; after the shift, cell k shows what was at k + n: it is right where
    ; row[k] == row[k + n]. Count the others (sky: none); up to 8 are redrawn
    ; (marked) rather than moving the row's pixels (~600 T-states a cell
    ; against ~6,000 a row, and most rows differ in a few cells)
    ld a,(sr_n)
    ld e,a
    ld d,0
    ex de,hl
    add hl,de                       ; HL = row + n, DE = row
    ld a,(sr_keep)
    ld b,a
    ld c,0
sr_cmp:
    ld a,(de)
    cp (hl)
    jr z,sr_cmp_same
    inc c
    ld a,c
    cp 9
    jr nc,sr_cmp_done               ; too many: move the row
sr_cmp_same:
    inc hl
    inc de
    djnz sr_cmp
sr_cmp_done:
    ld a,c
    ld (sr_moved),a
    or a
    jr z,sr_cmp_ok                  ; the same shifted
    cp 9
    jr nc,sr_cmp_ok                 ; moved
    ld b,a
    ld a,(sr_total)                 ; few: mark them, while the list has room
    add a,b
    cp 111
    jr nc,sr_cmp_ok
    ld (sr_total),a
    xor a
    ld (sr_moved),a
    ld hl,(sr_map)                  ; mark k where row[k] != row[k + n]
    ld a,(sr_n)
    ld e,a
    ld d,0
    ex de,hl
    add hl,de
    ld a,(sr_keep)
    ld b,a
    ld c,0
sr_dmark:
    ld a,(de)
    cp (hl)
    jr z,sr_dmark_next
    push bc
    push de
    push hl
    ld a,(sr_r)
    ld h,a
    ld l,c
    call _cs_mark_rc
    pop hl
    pop de
    pop bc
sr_dmark_next:
    inc hl
    inc de
    inc c
    djnz sr_dmark
sr_cmp_ok:
    ; old = what the screen shows at the edge (n bytes)
    ld hl,(sr_map)
    ld a,(sr_edge0)
    ld e,a
    ld d,0
    add hl,de
    ld de,sr_old
    ld a,(sr_n)
    ld c,a
    ld b,0
    ldir
    ; move the map row
    ld a,(sr_keep)
    ld c,a
    ld b,0
    ld a,(sr_left)
    or a
    jr z,sr_map_r
    ld de,(sr_map)                  ; left: row[0..] = row[n..]
    ld a,(sr_n)
    ld l,a
    ld h,0
    add hl,de
    ldir
    jr sr_map_done
sr_map_r:
    ld hl,(sr_map)                  ; right: from the end, row[keep - 1] -> row[cs_w - 1]
    dec c                           ; (keep - 1)
    add hl,bc
    inc c
    push hl
    ld a,(sr_n)
    ld e,a
    ld d,0
    add hl,de
    ex de,hl
    pop hl
    lddr
sr_map_done:
    ; the new cells from fill
    ld hl,(sr_map)
    ld a,(sr_edge0)
    ld e,a
    ld d,0
    add hl,de
    ex de,hl
    ld hl,(sr_fill)
    ld a,(sr_n)
    ld c,a
    ld b,0
    ldir
    ; the pixels and the colours
    ld a,(sr_moved)
    or a
    jr z,sr_marks
    ld a,(_cs_row)
    ld b,a
    ld a,(sr_r)
    add a,b
    ld b,a                          ; B = the screen row
    push bc
    add a,a
    add a,a
    add a,a                         ; its first pixel line
    ld l,a
    ld h,0
    add hl,hl
    ld de,_zx_row
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld a,(_cs_col)
    add a,e
    ld e,a                          ; DE = the line at the room's first column
    ld a,(sr_left)
    ld l,a
    ld h,0
    push hl                         ; left
    ld a,(sr_keep)
    ld h,a
    ld a,(sr_n)
    ld l,a
    push hl                         ; n, keep (one byte each)
    push de                         ; line
    call _cs_shift_lines
    pop af
    pop af
    pop af
    pop bc
    ld l,b                          ; attributes: 0x5800 + row * 32 + col
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld de,0x5800
    add hl,de
    ld a,(_cs_col)
    ld e,a
    ld d,0
    add hl,de                       ; HL = the row's attributes
    ld a,(sr_keep)
    ld c,a
    ld b,0
    ld a,(sr_left)
    or a
    jr z,sr_attr_r
    ld e,l
    ld d,h
    ld a,(sr_n)
    add a,l
    ld l,a                          ; (one 32-byte attribute row)
    ldir
    jr sr_marks
sr_attr_r:
    dec c
    add hl,bc
    inc c
    ld a,(sr_n)
    add a,l
    ld e,a
    ld d,h
    lddr
sr_marks:
    ; mark the new cells that differ from what the screen shows
    ld hl,(sr_fill)
    ld de,sr_old
    ld a,(sr_n)
    ld b,a
    ld a,(sr_edge0)
    ld c,a
sr_mark:
    ld a,(de)
    cp (hl)
    jr z,sr_mark_next
    push bc
    push de
    push hl
    ld a,(sr_r)
    ld h,a
    ld l,c
    call _cs_mark_rc
    pop hl
    pop de
    pop bc
sr_mark_next:
    inc hl
    inc de
    inc c
    djnz sr_mark
    ld (sr_fill),hl                 ; the next row's fill
    ld a,(_cs_h)
    ld b,a
    ld a,(sr_r)
    inc a
    ld (sr_r),a
    cp b
    jp c,sr_row
    pop ix
    ret

SECTION data_user

_cs_scroll_same: defw 0             ; cellspr48.h

SECTION bss_user

sr_fill:  defs 2
sr_map:   defs 2
sr_n:     defs 1
sr_keep:  defs 1
sr_left:  defs 1
sr_edge0: defs 1
sr_r:     defs 1
sr_moved: defs 1
sr_total: defs 1
sr_old:   defs 32
