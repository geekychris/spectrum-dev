; cellspr48 (see cellspr48.h): cs_refresh's loop. For each listed cell, lines
; first .. first+lines-1 of its tile go straight to the screen - unless a
; drawn sprite's box covers the cell, which is then marked (composed by the
; next cs_update). For animated tiles (conveyor belts): a few bytes a cell
; instead of a compose.
;
; In (set by cs_refresh in cellspr48.c): _cs_ref_c, _cs_ref_r (pointers to the
; columns and rows), _cs_ref_n, _cs_ref_first, _cs_ref_lines, and _cs_box:
; _cs_nbox boxes of c0, c1, r0, r1 (cells covered by drawn sprites).

SECTION code_user

defc CS_MAX_SPR = 16                ; cellspr48.h

PUBLIC _cs_refresh_loop
PUBLIC _cs_ref_c, _cs_ref_r, _cs_ref_n, _cs_ref_first, _cs_ref_lines, _cs_box, _cs_nbox
EXTERN _cs_map_row, _cs_tiles, _cs_col, _cs_row, _cs_mark_rc

_cs_refresh_loop:
    ld a,(_cs_ref_n)
    or a
    ret z
    ld (ref_left),a
ref_cell:
    ld hl,(_cs_ref_c)
    ld c,(hl)                       ; C = column
    inc hl
    ld (_cs_ref_c),hl
    ld hl,(_cs_ref_r)
    ld b,(hl)                       ; B = row
    inc hl
    ld (_cs_ref_r),hl
    ; under a sprite?
    ld a,(_cs_nbox)
    or a
    jr z,ref_draw
    ld e,a
    ld hl,_cs_box
ref_box:
    ld a,c
    cp (hl)                         ; c < c0
    jr c,ref_nobox
    inc hl
    ld a,(hl)
    cp c                            ; c1 < c
    jr c,ref_nobox1
    inc hl
    ld a,b
    cp (hl)                         ; r < r0
    jr c,ref_nobox2
    inc hl
    ld a,(hl)
    cp b                            ; r1 < r
    jr c,ref_nobox3
    ld h,b                          ; covered: compose it later
    ld l,c
    call _cs_mark_rc
    jr ref_next
ref_nobox:
    inc hl
ref_nobox1:
    inc hl
ref_nobox2:
    inc hl
ref_nobox3:
    inc hl
    dec e
    jr nz,ref_box
ref_draw:
    ; source: tiles + id * 8 + first
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
    ld l,(hl)
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    ld de,(_cs_tiles)
    add hl,de
    ld a,(_cs_ref_first)
    ld e,a
    ld d,0
    add hl,de
    ex de,hl                        ; DE = source
    ; screen: cell (c + cs_col, r + cs_row), line `first`
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
    ld l,a
    ld a,b
    and 0x18
    or 0x40
    ld h,a
    ld a,(_cs_ref_first)
    add a,h
    ld h,a
    ld a,(_cs_ref_lines)
    ld b,a
ref_line:
    ld a,(de)
    ld (hl),a
    inc de
    inc h
    djnz ref_line
ref_next:
    ld hl,ref_left
    dec (hl)
    jp nz,ref_cell
    ret

SECTION bss_user
_cs_ref_c:     defs 2
_cs_ref_r:     defs 2
_cs_ref_n:     defs 1
_cs_ref_first: defs 1
_cs_ref_lines: defs 1
_cs_nbox:      defs 1
_cs_box:       defs 4 * CS_MAX_SPR
ref_left:      defs 1
