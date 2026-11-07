; jq.c: void enemies_update(void) - the original's enemies_update() (enemy.c)
; for the Spectrum, in asm: zsdcc's code took ~2,600 T-states a beetle, a
; third of a frame for a level's enemies every tick; this is ~500. jq.c's C
; version is the host's (tools/solve.c) and the reference: test.py plays the
; solver's routes, which only clear the levels if both agree tick for tick.
;
; Enemy (jq.h): type 0, x 1, y 3, vx 5, vy 7, dir 9, alive 10, frame 11, timer 12

SECTION code_user


PUBLIC _enemies_update, _enemies_rows
EXTERN _game, tile_at, _jq_solid, _jq_level_w, _jq_fly_dy, _tmap_row, _tmap_w

defc E_TYPE = 0
defc E_X = 1
defc E_Y = 3
defc E_VX = 5
defc E_VY = 7
defc E_DIR = 9
defc E_ALIVE = 10
defc E_FRAME = 11
defc E_TIMER = 12
defc E_SIZE = 13
defc ENEMIES = 56                   ; offsetof(Game, enemies), checked by jq.c
defc MAX_ENEMIES = 16
defc TILE_PLATFORM = 10
defc WALK = 12                      ; walk_rows: 3 rows, then a column cache
defc W_COLL = 6                     ; left edge: column, wall?, floor?
defc W_COLR = 9                     ; right edge (x + 14): the same

_enemies_update:
    push ix
    push iy
    ld ix,_game + ENEMIES
    ld iy,walk_rows
    ld b,MAX_ENEMIES
eu_loop:
    push bc
    ld a,(ix+E_ALIVE)
    or a
    call nz,eu_one
    pop bc
    ld de,E_SIZE
    add ix,de
    ld de,WALK
    add iy,de
    djnz eu_loop
    pop iy
    pop ix
    ret

; void enemies_rows(void): after a level's enemies are placed. Beetles and
; ants never change y, and the one tile change in the game (a ? block hit)
; keeps the tile solid: the rows under y, y + 12 and y + 16 are kept for each
; (0 = off the map: nothing there)
_enemies_rows:
    push ix
    ld ix,_game + ENEMIES
    ld hl,walk_rows
    ld b,MAX_ENEMIES
er_loop:
    push bc
    ld c,0
    call er_row
    ld c,12
    call er_row
    ld c,16
    call er_row
    ld (hl),0xFF                    ; no column cached
    inc hl
    inc hl
    inc hl
    ld (hl),0xFF
    inc hl
    inc hl
    inc hl
    pop bc
    ld de,E_SIZE
    add ix,de
    djnz er_loop
    pop ix
    ret
er_row:                             ; (HL) = the row under y + C, HL += 2
    push hl
    ld l,(ix+E_Y)
    ld h,(ix+E_Y+1)
    ld b,0
    add hl,bc                       ; py
    ld de,0
    ld a,h                          ; 0 <= py < 224
    or a
    jr nz,er_store
    ld a,l
    cp 224
    jr nc,er_store
    rrca
    rrca
    rrca
    and 0x1E
    ld l,a
    ld h,0
    ld bc,_tmap_row
    add hl,bc
    ld e,(hl)
    inc hl
    ld d,(hl)
er_store:
    pop hl
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ret

; A = the tile in row HL (0: none) at pixel column DE (C changed)
col_tile:
    ld a,h
    or l
    ret z                           ; (A = 0)
    bit 7,d
    jr z,ct_pos
    ld a,e                          ; -15..-1: column 0
    cp 0xF1
    jr c,ct_none
    inc d
    jr nz,ct_none
    ld a,(hl)
    ret
ct_pos:
    ld a,d
    and 0xF0
    jr nz,ct_none
    ld a,e
    srl d
    rra
    srl d
    rra
    srl d
    rra
    srl d
    rra
    ld c,a
    ld a,(_tmap_w)
    dec a
    cp c
    jr c,ct_none
    ld a,c
    add a,l
    ld l,a
    jr nc,ct_1
    inc h
ct_1:
    ld a,(hl)
    ret
ct_none:
    xor a
    ret

; HL = HL >> 6 (arithmetic): the top 16 bits of HL << 2, sign-extended
sra6:
    ld a,h
    rlca
    sbc a,a                         ; A = the sign
    add hl,hl
    rla
    add hl,hl
    rla
    ld l,h
    ld h,a
    ret

; A = solid[tile at (DE, HL)]
solid_at:
    call tile_at
solid_of:                           ; A = solid[A]
    ld hl,_jq_solid
    add a,l
    ld l,a
    jr nc,so_1
    inc h
so_1:
    ld a,(hl)
    ret

eu_one:
    inc (ix+E_TIMER)                ; timer++; frame = (timer >> 3) & 1
    ld a,(ix+E_TIMER)
    rrca
    rrca
    rrca
    and 1
    ld (ix+E_FRAME),a
    ld a,(ix+E_TYPE)
    cp 2
    jr z,eu_walker
    cp 5
    jr z,eu_walker
    cp 3
    jp z,eu_fly
    cp 4
    jp z,eu_spider
    ret

; ---- beetle, ant: walk, turn at walls and at ledges
eu_walker:
    ld l,(ix+E_VX)
    ld h,(ix+E_VX+1)
    call sra6
    ex de,hl                        ; DE = vx >> 6
    ld l,(ix+E_X)
    ld h,(ix+E_X+1)
    bit 7,(ix+E_DIR)
    jr z,ew_right
    or a
    sbc hl,de
    jr ew_moved
ew_right:
    add hl,de
ew_moved:
    ld (ix+E_X),l
    ld (ix+E_X+1),h
    ; the walls and floors under its two edges (columns cached: they only
    ; change when an edge moves into another tile column)
    ld l,(ix+E_X)
    ld h,(ix+E_X+1)
    bit 7,h
    jp nz,ew_slow                   ; x < 0: look the tiles up (never seen)
    push hl
    call col_of
    cp (iy+W_COLL)
    jr z,ew_l_cached
    ld (iy+W_COLL),a
    call x_left
    call edge_tiles
    ld (iy+W_COLL+1),c
    ld (iy+W_COLL+2),b
ew_l_cached:
    pop hl
    ld de,14
    add hl,de
    call col_of
    cp (iy+W_COLR)
    jr z,ew_r_cached
    ld (iy+W_COLR),a
    call x_right
    call edge_tiles
    ld (iy+W_COLR+1),c
    ld (iy+W_COLR+2),b
ew_r_cached:
    ; wall: the leading edge
    bit 7,(ix+E_DIR)
    jr z,ew_wall_r
    ld a,(iy+W_COLL+1)
    or a
    jr z,ew_edge
    ld (ix+E_DIR),1
    jr ew_edge
ew_wall_r:
    ld a,(iy+W_COLR+1)
    or a
    jr z,ew_edge
    ld (ix+E_DIR),0xFF
ew_edge:                            ; ledge: no floor (solid or platform) at y + 16
    bit 7,(ix+E_DIR)
    jr z,ew_edge_r
    ld a,(iy+W_COLL+2)
    or a
    jr nz,ew_edge_r
    ld (ix+E_DIR),1
ew_edge_r:
    ld a,(ix+E_DIR)                 ; dir > 0
    dec a
    bit 7,a
    ret nz
    ld a,(iy+W_COLR+2)
    or a
    ret nz
    ld (ix+E_DIR),0xFF
    ret

; A = HL >> 4 (HL = 0..4095; more is column 255, never a level's)
col_of:
    ld a,h
    and 0xF0
    ld a,255
    ret nz
    ld a,l
    srl h
    rra
    srl h
    rra
    srl h
    rra
    srl h
    rra
    ret

; the edge at pixel column DE: C = wall (solid at y or y + 12), B = floor
; (solid or a platform at y + 16)
edge_tiles:
    push de
    call corner_y
    pop de
    jr nz,et_wall
    push de
    call corner_y12
    pop de
et_wall:
    ld c,a
    push bc
    call floor_at
    pop bc
    ld b,a
    ret

; x < 0: the original's lookups, uncached
ew_slow:
    bit 7,(ix+E_DIR)
    jr z,ews_wall_r
    call corner_l_y
    jr nz,ews_turn_r
    call corner_l_y12
    jr z,ews_edge
ews_turn_r:
    ld (ix+E_DIR),1
    jr ews_edge
ews_wall_r:
    call corner_r_y
    jr nz,ews_turn_l
    call corner_r_y12
    jr z,ews_edge
ews_turn_l:
    ld (ix+E_DIR),0xFF
ews_edge:
    bit 7,(ix+E_DIR)
    jr z,ews_edge_r
    call x_left
    call floor_at
    jr nz,ews_edge_r
    ld (ix+E_DIR),1
ews_edge_r:
    ld a,(ix+E_DIR)
    dec a
    bit 7,a
    ret nz
    call x_right
    call floor_at
    ret nz
    ld (ix+E_DIR),0xFF
    ret

; NZ if the tile at (DE, y + 16) is solid or a platform
floor_at:
    ld l,(iy+4)
    ld h,(iy+5)                     ; the row under y + 16
    call col_tile
    cp TILE_PLATFORM
    jr z,fa_yes
    call solid_of
    or a
    ret
fa_yes:
    or a                            ; A = 10: NZ
    ret

; NZ if solid at (x, y) / (x, y + 12) / (x + 14, y) / (x + 14, y + 12)
corner_l_y:
    call x_left
    jr corner_y
corner_l_y12:
    call x_left
    jr corner_y12
corner_r_y:
    call x_right
corner_y:
    ld l,(iy+0)
    ld h,(iy+1)
    jr corner
corner_r_y12:
    call x_right
corner_y12:
    ld l,(iy+2)
    ld h,(iy+3)
corner:
    call col_tile
    call solid_of
    or a
    ret
x_left:                             ; DE = x
    ld e,(ix+E_X)
    ld d,(ix+E_X+1)
    ret
x_right:                            ; DE = x + 14
    ld l,(ix+E_X)
    ld h,(ix+E_X+1)
    ld de,14
    add hl,de
    ex de,hl
    ret

; ---- fly: a sine wave, turning at the level's sides and every 121 ticks
eu_fly:
    ld l,(ix+E_VX)
    ld h,(ix+E_VX+1)
    call sra6
    ex de,hl
    ld l,(ix+E_X)
    ld h,(ix+E_X+1)
    bit 7,(ix+E_DIR)
    jr z,ef_right
    or a
    sbc hl,de
    jr ef_moved
ef_right:
    add hl,de
ef_moved:
    ld (ix+E_X),l
    ld (ix+E_X+1),h
    ld a,(ix+E_TIMER)               ; y += fly_dy[timer & 31]
    and 31
    ld e,a
    ld d,0
    ld hl,_jq_fly_dy
    add hl,de
    ld e,(hl)
    ld a,e
    rlca
    sbc a,a
    ld d,a                          ; DE = the step, sign-extended
    ld l,(ix+E_Y)
    ld h,(ix+E_Y+1)
    add hl,de
    ld (ix+E_Y),l
    ld (ix+E_Y+1),h
    ld l,(ix+E_X)                   ; x < 0 or x >= level_w - 16: turn
    ld h,(ix+E_X+1)
    bit 7,h
    jr nz,ef_turn
    ex de,hl
    ld hl,(_jq_level_w)
    ld bc,-16
    add hl,bc                       ; HL = level_w - 16 (> 0)
    ex de,hl
    or a
    sbc hl,de                       ; x - (level_w - 16): no borrow = x >= it
    jr c,ef_timer
ef_turn:
    call turn
ef_timer:
    ld a,(ix+E_TIMER)               ; timer > 120 (signed): turn, timer = 0
    bit 7,a
    ret nz
    cp 121
    ret c
    call turn
    ld (ix+E_TIMER),0
    ret

turn:                               ; dir = -dir
    ld a,(ix+E_DIR)
    neg
    ld (ix+E_DIR),a
    ret

; ---- spider: drops, climbs back
eu_spider:
    ld l,(ix+E_VY)
    ld h,(ix+E_VY+1)
    call sra6
    ex de,hl
    ld l,(ix+E_Y)
    ld h,(ix+E_Y+1)
    bit 7,(ix+E_DIR)
    jr z,es_down
    or a
    sbc hl,de
    jr es_moved
es_down:
    add hl,de
es_moved:
    ld (ix+E_Y),l
    ld (ix+E_Y+1),h
    ld a,(ix+E_DIR)                 ; dir > 0: dropping
    dec a
    bit 7,a
    jr nz,es_climbing
    ld e,(ix+E_X)
    ld d,(ix+E_X+1)
    ld bc,14
    add hl,bc                       ; HL = y + 14
    call solid_at
    or a
    jr z,es_drop_timer
    ld (ix+E_DIR),0xFF
es_drop_timer:
    ld a,(ix+E_TIMER)               ; timer > 100
    bit 7,a
    ret nz
    cp 101
    ret c
    ld (ix+E_DIR),0xFF
    ld (ix+E_TIMER),0
    ret
es_climbing:
    ld a,(ix+E_TIMER)               ; timer > 60
    bit 7,a
    ret nz
    cp 61
    ret c
    ld (ix+E_DIR),1
    ld (ix+E_TIMER),0
    ret

SECTION bss_user

walk_rows:  defs MAX_ENEMIES * WALK ; per enemy: the rows under y, y + 12, y + 16;
                                    ; the column cache (W_COLL, W_COLR)
