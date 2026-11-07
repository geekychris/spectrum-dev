; Sky Knights: the sprite pass of main.c in assembly (zsdcc's C took ~1100
; T-states for a slot with nothing in it and ~5000 for a moving knight; the
; C is kept in main.c under DRAW_IN_C). For every pod, enemy and player:
; where and how it should be drawn now, from the game's structs; if that
; changed, XOR it off its old place and onto the new one; then, if any
; object moved to other cells or changed colour, restore all their cells
; and paint them all again (tint_pass).
;
; Layouts (main.c checks them at boot and logs "sky: layout mismatch"):
;   Slot   {int16 x; uint8 y, on, w, h, attr; ptr img; uint8 col, row, tw, th, ch}
;   Egg    {x, y, dy, active, timer, ...}                 14 bytes
;   Enemy  {x, y, dx, dy, type, active, facing, anim_frame, ...}   22 bytes
;   Player {x, y, dx, dy, active, alive, lives, facing, anim_frame,
;           anim_timer, on_ground, respawn_timer, flapping}         26 bytes

SECTION code_user

PUBLIC _draw_pass, _tint_pass
EXTERN _slots, _rider_ptr, _dr_players, _dr_enemies, _dr_eggs, _dr_nplayers
EXTERN _tint_dirty, _enemy_attr
EXTERN asm_sprc_xor, asm_fix_int
EXTERN _cells_restore, _cells_paint, _cl_off, _cl_w, _cl_h, _cl_attr

DEFC SX_TAB  = 0x6080           ; mem.h
DEFC SY_TAB  = 0x6340
DEFC PRE_EGG = 0x6000

DEFC S_X = 0
DEFC S_Y = 2
DEFC S_ON = 3
DEFC S_W = 4
DEFC S_H = 5
DEFC S_ATTR = 6
DEFC S_IMG = 7
DEFC S_COL = 9
DEFC S_ROW = 10
DEFC S_TW = 11
DEFC S_TH = 12
DEFC S_CH = 13
DEFC SLOT_SIZE = 14
DEFC NUM_SLOTS = 18

DEFC A_EGG = 0x44               ; INK_GREEN | BRIGHT
DEFC A_EGG_HATCH = 0x43         ; INK_MAGENTA | BRIGHT
DEFC A_P1 = 0x46                ; INK_YELLOW | BRIGHT
DEFC A_P2 = 0x45                ; INK_CYAN | BRIGHT

; HL = world x (pixels) -> HL = screen x (sx_tab, x clamped to -20..320)
sx_of:
    ld de,20
    add hl,de
    bit 7,h
    jr z,sx_pos
    ld hl,0
sx_pos:
    push hl
    ld de,341
    or a
    sbc hl,de
    pop hl
    jr c,sx_in
    ld hl,340
sx_in:
    add hl,hl
    ld de,SX_TAB
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ret

; HL = world y (pixels) -> A = screen y (sy_tab, y clamped to 0..256)
sy_of:
    bit 7,h
    jr z,sy_pos
    ld hl,0
sy_pos:
    ld a,h
    or a
    jr z,sy_in
    ld hl,256
sy_in:
    ld de,SY_TAB
    add hl,de
    ld a,(hl)
    ret

; a rider at world (IY+0, IY+2) -> n_x, n_y (one line up: the head)
rider_pos:
    ld l,(iy+0)
    ld h,(iy+1)
    call asm_fix_int
    call sx_of
    ld (n_x),hl
    ld l,(iy+2)
    ld h,(iy+3)
    call asm_fix_int
    call sy_of
    dec a
    ld (n_y),a
    ret

; A = frame 0..7 -> n_img = rider_ptr[A]
rider_img:
    add a,a
    ld l,a
    ld h,0
    ld de,_rider_ptr
    add hl,de
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld (n_img),hl
    ret

_draw_pass:
    push ix
    push iy
    ld ix,_slots
    ; ---- pods
    ld iy,(_dr_eggs)
    ld b,8
dp_egg:
    push bc
    xor a
    ld (n_on),a
    ld a,(iy+6)                 ; active
    or (iy+7)
    jr z,dp_egg_set
    ld l,(iy+0)
    ld h,(iy+1)
    call asm_fix_int            ; HL = x
    ld c,A_EGG
    ld e,(iy+8)                 ; timer (1..300)
    ld a,(iy+9)
    or a
    jr nz,dp_egg_draw
    ld a,e
    cp 240                      ; EGG_WOBBLE_TIME: flash, then shake
    jr nc,dp_egg_draw
    bit 2,e
    jr z,dp_egg_col
    ld c,A_EGG_HATCH
dp_egg_col:
    cp 60
    jr nc,dp_egg_draw
    bit 1,e
    jr z,dp_egg_left
    inc hl
    jr dp_egg_draw
dp_egg_left:
    dec hl
dp_egg_draw:
    ld a,c
    ld (n_attr),a
    call sx_of
    ld (n_x),hl
    ld l,(iy+2)
    ld h,(iy+3)
    call asm_fix_int
    call sy_of
    ld (n_y),a
    ld hl,PRE_EGG
    ld (n_img),hl
    ld a,1
    ld (n_on),a
dp_egg_set:
    call slot_update
    ld de,14
    add iy,de
    ld de,SLOT_SIZE
    add ix,de
    pop bc
    djnz dp_egg
    ; ---- enemies
    ld iy,(_dr_enemies)
    ld b,8
dp_enemy:
    push bc
    xor a
    ld (n_on),a
    ld a,(iy+10)                ; active
    or (iy+11)
    jr z,dp_enemy_set
    call rider_pos
    ld a,(iy+8)                 ; attr = enemy_attr[type]
    ld l,a
    ld h,0
    ld de,_enemy_attr
    add hl,de
    ld a,(hl)
    ld (n_attr),a
    ld c,0                      ; frame: facing * 4 + anim_frame * 3 (wings and legs)
    ld a,(iy+12)
    or (iy+13)
    jr z,dp_enemy_r
    ld c,4
dp_enemy_r:
    ld a,(iy+14)
    or (iy+15)
    jr z,dp_enemy_f
    inc c
    inc c
    inc c
dp_enemy_f:
    ld a,c
    call rider_img
    ld a,1
    ld (n_on),a
dp_enemy_set:
    call slot_update
    ld de,22
    add iy,de
    ld de,SLOT_SIZE
    add ix,de
    pop bc
    djnz dp_enemy
    ; ---- players
    ld iy,(_dr_players)
    ld b,0                      ; B = player number
dp_player:
    push bc
    xor a
    ld (n_on),a
    ld a,(_dr_nplayers)
    cp b
    jr z,dp_player_set
    jr c,dp_player_set
    ld a,(iy+10)                ; alive
    or (iy+11)
    jr z,dp_player_set
    ld a,b
    or a
    ld a,A_P1
    jr z,dp_p1
    ld a,A_P2
dp_p1:
    ld (n_attr),a
    call rider_pos
    ld c,0                      ; frame: facing * 4 + flapping * 2 + anim_frame
    ld a,(iy+14)
    or (iy+15)
    jr z,dp_player_r
    ld c,4
dp_player_r:
    ld a,(iy+24)
    or (iy+25)
    jr z,dp_player_w
    inc c
    inc c
dp_player_w:
    ld a,(iy+16)
    or (iy+17)
    jr z,dp_player_f
    inc c
dp_player_f:
    ld a,c
    call rider_img
    ld a,1
    ld (n_on),a
dp_player_set:
    call slot_update
    ld de,26
    add iy,de
    ld de,SLOT_SIZE
    add ix,de
    pop bc
    inc b
    ld a,b
    cp 2
    jr nz,dp_player
    pop iy
    pop ix
    ret

; IX = slot; n_on, n_x, n_y, n_img, n_attr = what it should show now
slot_update:
    ld a,(n_on)
    cp (ix+S_ON)
    jr nz,su_change
    or a
    jp z,su_attr                ; off and still off
    ld hl,(n_x)
    ld a,l
    cp (ix+S_X)
    jr nz,su_change
    ld a,h
    cp (ix+S_X+1)
    jr nz,su_change
    ld a,(n_y)
    cp (ix+S_Y)
    jr nz,su_change
    ld hl,(n_img)
    ld a,l
    cp (ix+S_IMG)
    jr nz,su_change
    ld a,h
    cp (ix+S_IMG+1)
    jp z,su_attr
su_change:
    ld a,(ix+S_ON)              ; off the old place
    or a
    jr z,su_draw
    ld l,(ix+S_X)
    ld h,(ix+S_X+1)
    ld e,(ix+S_IMG)
    ld d,(ix+S_IMG+1)
    ld b,(ix+S_W)
    ld c,(ix+S_H)
    ld a,(ix+S_Y)
    call asm_sprc_xor
su_draw:
    ld a,(n_on)                 ; onto the new one
    or a
    jr z,su_cells
    ld hl,(n_x)
    ld de,(n_img)
    ld b,(ix+S_W)
    ld c,(ix+S_H)
    ld a,(n_y)
    call asm_sprc_xor
su_cells:                       ; other cells: the colours need doing
    ld a,(n_on)
    cp (ix+S_ON)
    jr nz,su_dirty
    ld hl,(n_x)
    ld a,(ix+S_X)
    xor l
    and 0xF8
    jr nz,su_dirty
    ld a,(ix+S_X+1)
    xor h
    jr nz,su_dirty
    ld a,(n_y)
    xor (ix+S_Y)
    and 0xF8
    jr z,su_store
su_dirty:
    ld a,1
    ld (_tint_dirty),a
    ld (ix+S_CH),a
su_store:
    ld a,(n_on)
    ld (ix+S_ON),a
    ld hl,(n_x)
    ld (ix+S_X),l
    ld (ix+S_X+1),h
    ld a,(n_y)
    ld (ix+S_Y),a
    ld hl,(n_img)
    ld (ix+S_IMG),l
    ld (ix+S_IMG+1),h
su_attr:
    ld a,(n_on)
    or a
    jr z,su_attr_set
    ld a,(n_attr)
    cp (ix+S_ATTR)
    jr z,su_attr_set
    ld a,1
    ld (_tint_dirty),a
    ld (ix+S_CH),a
su_attr_set:
    ld a,(n_attr)
    ld (ix+S_ATTR),a
    ret

; cl_off/cl_w/cl_h from the slot's cells (IX)
cells_of_slot:
    ld l,(ix+S_ROW)
    ld h,0
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld a,(ix+S_COL)
    or l
    ld l,a
    ld (_cl_off),hl
    ld a,(ix+S_TW)
    ld (_cl_w),a
    ld a,(ix+S_TH)
    ld (_cl_h),a
    ret

; if anything moved to other cells or changed colour: restore the cells of
; those that did, then paint every visible object that did or that touches
; a restored area, in slot order (the last painted wins where they overlap)
_tint_pass:
    ld a,(_tint_dirty)
    or a
    ret z
    xor a
    ld (_tint_dirty),a
    ld (tp_nrest),a
    push ix
    ld ix,_slots
    ld b,NUM_SLOTS
    ld hl,tp_rest               ; the restored areas: col, last col, row, last row
tp_restore:
    push bc
    ld a,(ix+S_CH)
    or a
    jr z,tp_restore_next
    ld a,(ix+S_TH)
    or a
    jr z,tp_restore_next
    push hl
    call cells_of_slot
    call _cells_restore
    pop hl
    ld a,(ix+S_COL)
    ld (hl),a
    inc hl
    add a,(ix+S_TW)
    dec a
    ld (hl),a
    inc hl
    ld a,(ix+S_ROW)
    ld (hl),a
    inc hl
    add a,(ix+S_TH)
    dec a
    ld (hl),a
    inc hl
    ld a,(tp_nrest)
    inc a
    ld (tp_nrest),a
tp_restore_next:
    ld de,SLOT_SIZE
    add ix,de
    pop bc
    djnz tp_restore
    ld ix,_slots
    ld b,NUM_SLOTS
tp_paint:
    push bc
    ld a,(ix+S_CH)
    or a
    jr nz,tp_area
    ld a,(ix+S_TH)              ; unchanged: repaint only if it touches a restored area
    or a
    jp z,tp_paint_next
    call touches_restored
    jp nc,tp_paint_next
    jp tp_paint_it
tp_area:
    ld (ix+S_CH),0
    ld (ix+S_TH),0
    ld a,(ix+S_ON)
    or a
    jp z,tp_paint_next
    ld a,(ix+S_Y)
    cp 192
    jp nc,tp_paint_next
    ; x1 = x + w * 8 - 1
    ld a,(ix+S_W)
    add a,a
    add a,a
    add a,a
    dec a
    ld e,a
    ld d,0
    ld l,(ix+S_X)
    ld h,(ix+S_X+1)
    add hl,de                   ; HL = x1
    ld a,(ix+S_X+1)
    or a
    jr z,tp_x_pos
    inc a
    jp nz,tp_paint_next         ; x > 255
    bit 7,h
    jp nz,tp_paint_next         ; x1 < 0: all off the left
    ld c,0                      ; C = first column
    jr tp_x1
tp_x_pos:
    ld a,(ix+S_X)
    rrca
    rrca
    rrca
    and 0x1F
    ld c,a
tp_x1:
    ld a,h                      ; last column: x1 >> 3, at most 31
    or a
    ld a,31
    jr nz,tp_x1_ok
    ld a,l
    rrca
    rrca
    rrca
    and 0x1F
tp_x1_ok:
    sub c
    inc a
    ld (ix+S_TW),a
    ld (ix+S_COL),c
    ld a,(ix+S_Y)
    rrca
    rrca
    rrca
    and 0x1F
    ld (ix+S_ROW),a
    ld c,a
    ld a,(ix+S_Y)               ; last row: (y + h - 1) >> 3, at most 23
    add a,(ix+S_H)
    dec a
    rrca
    rrca
    rrca
    and 0x1F
    cp 24
    jr c,tp_r1
    ld a,23
tp_r1:
    sub c
    inc a
    ld (ix+S_TH),a
tp_paint_it:
    call cells_of_slot
    ld a,(ix+S_ATTR)
    ld (_cl_attr),a
    call _cells_paint
tp_paint_next:
    ld de,SLOT_SIZE
    add ix,de
    pop bc
    dec b
    jp nz,tp_paint
    pop ix
    ret

; carry set if the slot's cells (IX) overlap one of the restored areas
touches_restored:
    ld a,(tp_nrest)
    or a
    ret z                       ; (carry clear)
    ld b,a
    ld hl,tp_rest
    ld a,(ix+S_COL)
    ld c,a                      ; C = first column
    add a,(ix+S_TW)
    dec a
    ld d,a                      ; D = last column
    ld a,(ix+S_ROW)
    ld e,a                      ; E = first row
tr_loop:
    ld a,d                      ; my first col <= its last, its first <= my last
    cp (hl)
    jr c,tr_no                  ; my last < its first
    inc hl
    ld a,(hl)
    cp c
    jr c,tr_no1                 ; its last < my first
    inc hl
    ld a,e                      ; rows: my first row + th - 1 >= its first row
    add a,(ix+S_TH)
    dec a
    cp (hl)
    jr c,tr_no2
    inc hl
    ld a,(hl)
    cp e
    jr c,tr_no3                 ; its last row < my first row
    scf
    ret
tr_no:
    inc hl
tr_no1:
    inc hl
tr_no2:
    inc hl
tr_no3:
    inc hl
    djnz tr_loop
    or a
    ret

SECTION bss_user
n_on:   defb 0
n_x:    defw 0
n_y:    defb 0
n_img:  defw 0
n_attr: defb 0
tp_nrest: defb 0
tp_rest: defs 4 * NUM_SLOTS
