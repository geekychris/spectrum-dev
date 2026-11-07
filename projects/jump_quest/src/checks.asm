; jq.c's per-tick box tests and particle update, in asm (zsdcc's took
; ~11,000 T-states a tick for the 24 items alone). The C versions in jq.c are
; the host's and the reference. Each overlap test is the original's
;     a.right > b.left + m  &&  a.left < b.left + n
; written as one unsigned compare: (uint16)(b.left - a.left + n - 1) < a.w + n - m - 1
;
; Item (jq.h):  type 0, x 1, y 3, active 5, frame 6, collected 7, float_off 8 (9 bytes)
; Enemy:        type 0, x 1, y 3, ..., alive 10 (13 bytes)
; Particle:     x 0, y 2, vx 4, vy 6, life 8, color 9 (10 bytes)

SECTION code_user


PUBLIC _items_check_collect, _enemies_check, _particles_update, _enemies_on_screen
EXTERN _game

defc ENEMIES = 56                   ; offsetof(Game, enemies) (checked in jq.c)
defc ITEMS = 56 + 16 * 13           ; offsetof(Game, items)
defc PARTICLES = ITEMS + 24 * 9     ; offsetof(Game, particles)

; args (px, py, pw, ph) -> the four constants of the two range tests:
;   kx = n - 1 - px, lx = pw + n - m - 1 (x), ky, ly (y; ly from ph)
; in: A = n - 1, C = n - m - 1 (x and y alike); sets kx, lx, ky, ly
setup:
    ld hl,4                         ; (setup's return + the caller's)
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                       ; px
    inc hl
    push hl
    ld l,a
    ld h,0
    or a
    sbc hl,de
    ld (kx),hl                      ; n - 1 - px
    pop hl
    ld e,(hl)
    inc hl
    ld d,(hl)                       ; py
    inc hl
    push hl
    ld l,a
    ld h,0
    or a
    sbc hl,de
    ld (ky),hl
    pop hl
    ld e,(hl)
    inc hl
    ld d,(hl)                       ; pw
    inc hl
    push hl
    ld l,c
    ld h,0
    add hl,de
    ld (lx),hl
    pop hl
    ld e,(hl)
    inc hl
    ld d,(hl)                       ; ph
    ld l,c
    ld h,0
    add hl,de
    ld (ly),hl
    ret

; int8_t items_check_collect(px, py, pw, ph): px + pw > x + 2 && px < x + 14
; (n = 14, m = 2), the same in y; the first such item is collected
_items_check_collect:
    ld a,13
    ld c,11
    call setup
    push ix
    ld ix,_game + ITEMS
    ld b,24
icc_loop:
    ld a,(ix+5)
    or a
    jr z,icc_next
    ld l,(ix+1)
    ld h,(ix+2)
    ld de,(kx)
    add hl,de
    ld a,h                          ; far away: x + kx outside 0..255
    or a
    jr nz,icc_next
    ld de,(lx)
    or a
    sbc hl,de
    jr nc,icc_next
    ld l,(ix+3)
    ld h,(ix+4)
    ld de,(ky)
    add hl,de
    ld de,(ly)
    or a
    sbc hl,de
    jr nc,icc_next
    ld (ix+5),0                     ; active = 0
    ld l,(ix+0)                     ; the type
    pop ix
    ret
icc_next:
    ld de,9
    add ix,de
    djnz icc_loop
    pop ix
    ld l,0
    ret

; uint8_t enemies_check(px, py, pw, ph, stomp): the original's two tests in
; one pass. With stomp (the player falling): the first enemy whose top the
; feet are on (x + 2 .. x + 14, feet strictly between y and y + 10) dies: 2.
; Otherwise 1 if one touches (x + 3 .. x + 13, y + 3 .. y + 13), else 0.
_enemies_check:
    ld a,12                         ; the touch box
    ld c,9
    call setup
    ld hl,(kx)
    inc hl
    ld (kx2),hl                     ; the stomp box's x: n = 14, m = 2
    ld hl,(lx)
    inc hl
    inc hl
    ld (lx2),hl
    ld hl,4                         ; feet - 1 = py + ph - 1
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    inc hl
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    add hl,de
    dec hl
    ld (feet1),hl
    ld hl,10                        ; stomp?
    add hl,sp
    ld a,(hl)
    ld (stomp),a
    xor a
    ld (touched),a
    push ix
    ld ix,_game + ENEMIES
    ld b,16
ec_loop:
    ld a,(ix+10)                    ; alive
    or a
    jr z,ec_next
    ld l,(ix+1)
    ld h,(ix+2)                     ; x
    push hl                         ; far away (x + kx2 outside 0..255): neither test
    ld de,(kx2)
    add hl,de
    ld a,h
    pop hl
    or a
    jr nz,ec_next
    ld a,(stomp)
    or a
    jr z,ec_touch
    push hl                         ; stomp: x + kx2 < lx2, feet - 1 - y < 9
    ld de,(kx2)
    add hl,de
    ld de,(lx2)
    or a
    sbc hl,de
    pop hl
    jr nc,ec_touch
    push hl
    ld hl,(feet1)
    ld e,(ix+3)
    ld d,(ix+4)
    or a
    sbc hl,de
    ld de,9
    or a
    sbc hl,de
    pop hl
    jr nc,ec_touch
    ld (ix+10),0                    ; it dies
    pop ix
    ld l,2
    ret
ec_touch:
    ld a,(touched)
    or a
    jr nz,ec_next                   ; (one is enough; a stomp may still come)
    ld de,(kx)
    add hl,de
    ld de,(lx)
    or a
    sbc hl,de
    jr nc,ec_next
    ld l,(ix+3)
    ld h,(ix+4)
    ld de,(ky)
    add hl,de
    ld de,(ly)
    or a
    sbc hl,de
    jr nc,ec_next
    ld a,1
    ld (touched),a
    ld a,(stomp)                    ; no stomp to look for: done
    or a
    jr z,ec_done
ec_next:
    ld de,13
    add ix,de
    dec b
    jp nz,ec_loop
ec_done:
    pop ix
    ld a,(touched)
    ld l,a
    ret

; void particles_update(void): the live ones move (x += vx, y += vy), fall
; (vy += 1) and age
_particles_update:
    push ix
    ld ix,_game + PARTICLES
    ld b,16
pu_loop:
    ld a,(ix+8)                     ; life > 0 (signed)
    dec a
    bit 7,a
    jr nz,pu_next
    ld l,(ix+0)
    ld h,(ix+1)
    ld e,(ix+4)
    ld d,(ix+5)
    add hl,de
    ld (ix+0),l
    ld (ix+1),h
    ld l,(ix+2)
    ld h,(ix+3)
    ld e,(ix+6)
    ld d,(ix+7)
    add hl,de
    ld (ix+2),l
    ld (ix+3),h
    inc de
    ld (ix+6),e
    ld (ix+7),d
    dec (ix+8)
pu_next:
    ld de,10
    add ix,de
    djnz pu_loop
    pop ix
    ret

; uint8_t enemies_on_screen(int16_t view_x, uint8_t *list): the indices of
; the live enemies wholly in the view (x - view_x in 0..240, y in 24..208:
; main.c's visible()), at most 5, in order; returns how many
_enemies_on_screen:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                       ; view_x
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld (eos_list),hl
    push ix
    ld ix,_game + ENEMIES
    ld bc,16 << 8                   ; B = 16 enemies, C = the index
    ld a,0
    ld (eos_n),a
eos_loop:
    ld a,(ix+10)
    or a
    jr z,eos_next
    ld l,(ix+1)
    ld h,(ix+2)
    or a
    sbc hl,de                       ; x - view_x <= 240 (unsigned)
    ld a,h
    or a
    jr nz,eos_next
    ld a,l
    cp 241
    jr nc,eos_next
    ld l,(ix+3)
    ld h,(ix+4)
    push de
    ld de,-24
    add hl,de                       ; y - 24 <= 184 (unsigned)
    pop de
    ld a,h
    or a
    jr nz,eos_next
    ld a,l
    cp 185
    jr nc,eos_next
    ld hl,(eos_list)
    ld (hl),c
    inc hl
    ld (eos_list),hl
    ld hl,eos_n
    inc (hl)
    ld a,(hl)
    cp 5
    jr z,eos_done
eos_next:
    push de
    ld de,13
    add ix,de
    pop de
    inc c
    djnz eos_loop
eos_done:
    pop ix
    ld a,(eos_n)
    ld l,a
    ret

SECTION bss_user

eos_list: defs 2
eos_n:    defs 1
kx: defs 2
lx: defs 2
ky: defs 2
ly: defs 2
kx2: defs 2
lx2: defs 2
feet1: defs 2
stomp: defs 1
touched: defs 1
