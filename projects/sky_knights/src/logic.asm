; Sky Knights: the per-frame loops of game.c in assembly - every enemy,
; every pod, every player-enemy pair - for zsdcc's C was ~5000 T-states an
; enemy and ~3000 a pair. What happens rarely calls back into the original
; C in game.c (an enemy's flap and its new flap time, raiders and wraiths
; turning to the nearest player, a joust, a pod hatching or being caught),
; so the rules themselves stay in C. The whole C is kept under PHYS_IN_C.
;
; Struct offsets (main.c checks them at boot):
;   Enemy  {x0 y2 dx4 dy6 type8 active10 facing12 anim_frame14 anim_timer16
;           flap_timer18 on_ground20}  22 bytes
;   Player {x0 y2 dx4 dy6 active8 alive10 ...}  26 bytes
;   Egg    {x0 y2 dy4 active6 timer8 on_ground10 hatch_type12}  14 bytes

SECTION code_user

PUBLIC _enemies_update, _eggs_update, _combat_scan
PUBLIC _lg_players, _lg_enemies, _lg_eggs, _lg_nplayers, _lg_frame
PUBLIC _jp_player, _jp_enemy, _jp_index
EXTERN _enemy_flap, _enemy_seek, _joust, _egg_hatch, _egg_pickup
EXTERN _body_step, _plat_scan, asm_fix_int, _rnd16, _gravity
EXTERN _phys_above, _ps_px, _ps_py, _ps_w, _ps_first

DEFC MAX_FALL = 256

; ---------------------------------------------------------------- enemies
_enemies_update:
    push ix
    ld ix,(_lg_enemies)
    ld b,8
eu_loop:
    push bc
    ld a,(ix+10)                ; active
    or (ix+11)
    jp z,eu_next
    ; AI: flap when the timer runs out
    ld l,(ix+18)
    ld h,(ix+19)
    dec hl
    ld (ix+18),l
    ld (ix+19),h
    bit 7,h
    jr nz,eu_flap
    ld a,h
    or l
    jr nz,eu_body
eu_flap:
    push ix
    pop hl
    call _enemy_flap            ; game.c: impulse + the next flap time
eu_body:
    xor a
    ld (_phys_above),a
    push ix
    pop hl
    call _body_step             ; gravity .. floor
    ld (ix+20),l
    ld (ix+21),0
    ld a,(ix+8)                 ; type
    or a
    jr z,eu_swooper
    ld a,(_lg_frame)            ; raiders and wraiths: every 64 frames
    and 63
    jr nz,eu_anim
    push ix
    pop hl
    call _enemy_seek            ; game.c: towards the nearest player
    jr eu_anim
eu_swooper:
    call _rnd16                 ; (rng() & 127) == 0, rng() = rnd16() >> 1
    ld a,l
    and 0xFE
    jr nz,eu_anim
    ld a,(ix+12)                ; turn round
    xor 1
    ld (ix+12),a
    ld hl,64                    ; ENEMY_SLOW
    jr z,eu_dx
    ld hl,-64
eu_dx:
    ld (ix+4),l
    ld (ix+5),h
eu_anim:
    ld a,(ix+16)                ; anim_timer
    inc a
    cp 9
    jr c,eu_anim_set
    ld a,(ix+14)
    xor 1
    ld (ix+14),a
    xor a
eu_anim_set:
    ld (ix+16),a
eu_next:
    ld de,22
    add ix,de
    pop bc
    dec b
    jp nz,eu_loop
    pop ix
    ret

; ---------------------------------------------------------------- pods
_eggs_update:
    push ix
    push iy
    ld ix,(_lg_eggs)
    ld b,8
eg_loop:
    push bc
    ld a,(ix+6)                 ; active
    or (ix+7)
    jp z,eg_next
    ld a,(ix+10)                ; falling?
    or (ix+11)
    jp nz,eg_timer
    ld l,(ix+4)                 ; dy += gravity, at most MAX_FALL_VEL
    ld h,(ix+5)
    ld de,(_gravity)
    add hl,de
    push hl
    ld de,MAX_FALL + 1
    or a
    sbc hl,de
    pop hl
    jp m,eg_dy_ok
    ld hl,MAX_FALL
eg_dy_ok:
    ld (ix+4),l
    ld (ix+5),h
    ex de,hl                    ; y += dy
    ld l,(ix+2)
    ld h,(ix+3)
    add hl,de
    ld (ix+2),l
    ld (ix+3),h
    call asm_fix_int            ; on a ledge? (8 x 8)
    ld de,8
    add hl,de
    ld (_ps_py),hl
    ld l,(ix+0)
    ld h,(ix+1)
    call asm_fix_int
    ld (_ps_px),hl
    ld a,8
    ld (_ps_w),a
    xor a
    ld (_ps_first),a
    call _plat_scan
    bit 7,h
    jr nz,eg_floor
    bit 7,(ix+5)                ; dy >= 0
    jr nz,eg_floor
    ld de,-8
    add hl,de
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    add hl,hl
    ld (ix+2),l
    ld (ix+3),h
    call eg_landed
eg_floor:
    ld l,(ix+2)                 ; y + 8 >= 256 -> y = 248
    ld h,(ix+3)
    ld de,248 * 64
    or a
    sbc hl,de
    jp m,eg_timer
    ld (ix+2),e
    ld (ix+3),d
    call eg_landed
eg_timer:
    ld l,(ix+8)                 ; timer--
    ld h,(ix+9)
    dec hl
    ld (ix+8),l
    ld (ix+9),h
    bit 7,h
    jr nz,eg_hatch
    ld a,h
    or l
    jr nz,eg_pickup
eg_hatch:
    push ix
    pop hl
    call _egg_hatch             ; game.c: a stronger enemy, the pod gone
    jr eg_next
eg_pickup:                      ; a living player touching it catches it
    ld l,(ix+0)
    ld h,(ix+1)
    call asm_fix_int
    ld (eg_ex),hl
    ld l,(ix+2)
    ld h,(ix+3)
    call asm_fix_int
    ld (eg_ey),hl
    ld iy,(_lg_players)
    ld b,0
eg_player:
    ld a,(_lg_nplayers)
    cp b
    jr z,eg_next
    jr c,eg_next
    ld a,(iy+10)                ; alive
    or (iy+11)
    jr z,eg_player_next
    push bc
    ld l,(iy+0)                 ; px + 20 > ex && px < ex + 8: px - ex in -19..7
    ld h,(iy+1)
    call asm_fix_int
    ld de,(eg_ex)
    or a
    sbc hl,de
    ld de,19
    add hl,de
    ld de,27
    or a
    sbc hl,de
    jr nc,eg_miss
    ld l,(iy+2)                 ; the same for y
    ld h,(iy+3)
    call asm_fix_int
    ld de,(eg_ey)
    or a
    sbc hl,de
    ld de,19
    add hl,de
    ld de,27
    or a
    sbc hl,de
    jr nc,eg_miss
    pop bc
    ld a,b
    ld (_jp_index),a
    push ix
    pop hl
    call _egg_pickup            ; game.c: the points, the pod gone
    jr eg_next
eg_miss:
    pop bc
eg_player_next:
    ld de,26
    add iy,de
    inc b
    jr eg_player
eg_next:
    ld de,14
    add ix,de
    pop bc
    dec b
    jp nz,eg_loop
    pop iy
    pop ix
    ret

eg_landed:
    xor a
    ld (ix+4),a                 ; dy = 0
    ld (ix+5),a
    ld (ix+10),1                ; on_ground
    ld (ix+11),a
    ret

; ---------------------------------------------------------------- jousts
; every living player against every active enemy: boxes overlapping
; (|px - ex| < 20 and |py - ey| < 20) -> joust() in game.c decides
_combat_scan:
    push ix
    push iy
    ld iy,(_lg_players)
    xor a
    ld (_jp_index),a
cs_player:
    ld a,(_lg_nplayers)
    ld b,a
    ld a,(_jp_index)
    cp b
    jp nc,cs_done
    ld a,(iy+10)                ; alive
    or (iy+11)
    jp z,cs_next_player
    ld l,(iy+0)                 ; the player's pixel position
    ld h,(iy+1)
    call asm_fix_int
    ld (cs_px),hl
    ld l,(iy+2)
    ld h,(iy+3)
    call asm_fix_int
    ld (cs_py),hl
    ld ix,(_lg_enemies)
    ld b,8
cs_enemy:
    ld a,(ix+10)                ; active
    or (ix+11)
    jr z,cs_next_enemy
    ld l,(ix+0)                 ; px - ex in -19..19
    ld h,(ix+1)
    call asm_fix_int
    ex de,hl
    ld hl,(cs_px)
    or a
    sbc hl,de
    ld de,19
    add hl,de
    ld de,39
    or a
    sbc hl,de
    jr nc,cs_next_enemy
    ld l,(ix+2)                 ; py - ey in -19..19
    ld h,(ix+3)
    call asm_fix_int
    ex de,hl
    ld hl,(cs_py)
    or a
    sbc hl,de
    ld de,19
    add hl,de
    ld de,39
    or a
    sbc hl,de
    jr nc,cs_next_enemy
    push bc
    ld (_jp_player),iy
    ld (_jp_enemy),ix
    call _joust                 ; L = 1: the player is down
    ld iy,(_jp_player)
    ld ix,(_jp_enemy)
    pop bc
    ld a,l
    or a
    jr nz,cs_next_player
    ld l,(iy+0)                 ; a bounce changed nothing, but be exact:
    ld h,(iy+1)                 ; the original re-reads the player each time
    call asm_fix_int
    ld (cs_px),hl
    ld l,(iy+2)
    ld h,(iy+3)
    call asm_fix_int
    ld (cs_py),hl
cs_next_enemy:
    ld de,22
    add ix,de
    djnz cs_enemy
cs_next_player:
    ld de,26
    add iy,de
    ld hl,_jp_index
    inc (hl)
    jp cs_player
cs_done:
    pop iy
    pop ix
    ret

SECTION bss_user
_lg_players:  defw 0
_lg_enemies:  defw 0
_lg_eggs:     defw 0
_lg_nplayers: defb 0
_lg_frame:    defb 0
_jp_player:   defw 0
_jp_enemy:    defw 0
_jp_index:    defb 0
eg_ex:        defw 0
cs_px:        defw 0
cs_py:        defw 0
eg_ey:        defw 0
