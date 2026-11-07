; Uranus Lander: the explosion's particles in assembly. zsdcc's 32-bit
; arithmetic made the original's update_particles() cost ~75,000 T-states a
; frame (more than a frame), so a crash slowed the game down.
;
; particles_update_asm: port: game.c's update_particles() loop, the same
;   16.16 arithmetic: x += vx, y += vy, vy += 600, life--, off at life <= 0
;   (and particles_live counts down).
; particles_project: view.c: the screen (x, y) pairs of the active particles
;   inside the world (0..319, 0..255) and the play rows (8..183):
;   x * 0.8 through wx2sx, y * 0.75 rounded. Returns how many.

SECTION code_user

PUBLIC _particles_update_asm, _particles_project
EXTERN _wx2sx

PSIZE equ 22                    ; x y vx vy (LONG), life color active (WORD)
NPART equ 32

; void particles_update_asm(Particle *p, WORD *live)
_particles_update_asm:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld (pu_live),hl
    ex de,hl                    ; HL = the particle
    ld b,NPART
pu_loop:
    push hl
    ld de,20
    add hl,de
    ld a,(hl)
    inc hl
    or (hl)                     ; active?
    pop hl
    jr z,pu_next
    push bc
    push hl
    ld d,h
    ld e,l
    push hl
    ld hl,8
    add hl,de
    ex de,hl                    ; DE = &vx
    pop hl                      ; HL = &x
    ld a,(de)                   ; x += vx
    add a,(hl)
    ld (hl),a
    inc hl
    inc de
    ld b,3
pu_x:
    ld a,(de)
    adc a,(hl)
    ld (hl),a
    inc hl
    inc de
    djnz pu_x
    ld a,(de)                   ; y += vy
    add a,(hl)
    ld (hl),a
    inc hl
    inc de
    ld b,3
pu_y:
    ld a,(de)
    adc a,(hl)
    ld (hl),a
    inc hl
    inc de
    djnz pu_y
    ex de,hl                    ; HL = &vy: vy += 600 (0x258)
    ld a,(hl)
    add a,0x58
    ld (hl),a
    inc hl
    ld a,(hl)
    adc a,0x02
    ld (hl),a
    inc hl
    ld a,(hl)
    adc a,0
    ld (hl),a
    inc hl
    ld a,(hl)
    adc a,0
    ld (hl),a
    inc hl                      ; HL = &life
    ld e,(hl)
    inc hl
    ld d,(hl)
    dec de                      ; life--
    ld (hl),d
    dec hl
    ld (hl),e
    bit 7,d
    jr nz,pu_dead
    ld a,d
    or e
    jr nz,pu_alive
pu_dead:
    ld de,4
    add hl,de                   ; &active
    xor a
    ld (hl),a
    inc hl
    ld (hl),a
    ld hl,(pu_live)
    ld e,(hl)
    inc hl
    ld d,(hl)
    dec de
    ld (hl),d
    dec hl
    ld (hl),e
pu_alive:
    pop hl
    pop bc
pu_next:
    ld de,PSIZE
    add hl,de
    djnz pu_loop
    ret

; uint8_t particles_project(const Particle *p, uint8_t *xy)
_particles_project:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ld (pp_out),hl
    ex de,hl                    ; HL = the particle
    xor a
    ld (pp_n),a
    ld b,NPART
pp_loop:
    push bc
    push hl
    ld de,20
    add hl,de
    ld a,(hl)
    inc hl
    or (hl)
    pop hl
    jr z,pp_next
    push hl
    ; wx = round(x): the high word, + 1 if bit 15
    inc hl
    ld a,(hl)                   ; x byte 1
    rlca
    and 1
    ld c,a
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl                      ; HL = &y
    ex de,hl
    ld b,0
    add hl,bc                   ; HL = wx
    bit 7,h
    jr nz,pp_skip               ; < 0
    ld bc,320
    or a
    sbc hl,bc
    jr nc,pp_skip               ; >= 320
    add hl,bc
    ld bc,_wx2sx
    add hl,bc
    ld a,(hl)
    ld (pp_sx),a
    ex de,hl                    ; HL = &y
    inc hl
    ld a,(hl)                   ; y byte 1
    rlca
    and 1
    ld c,a
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    ex de,hl
    ld b,0
    add hl,bc                   ; HL = wy
    ld a,h
    or a
    jr nz,pp_skip               ; outside 0..255
    ; sy = (wy * 3 + 2) / 4
    ld e,l
    ld d,0
    ld h,d
    add hl,hl
    add hl,de
    inc hl
    inc hl
    srl h
    rr l
    srl h
    rr l
    ld a,l
    cp 8
    jr c,pp_skip
    cp 184
    jr nc,pp_skip
    ld hl,(pp_out)
    ld c,a
    ld a,(pp_sx)
    ld (hl),a
    inc hl
    ld (hl),c
    inc hl
    ld (pp_out),hl
    ld hl,pp_n
    inc (hl)
pp_skip:
    pop hl
pp_next:
    ld de,PSIZE
    add hl,de
    pop bc
    djnz pp_loop
    ld a,(pp_n)
    ld l,a
    ret

SECTION bss_user

pu_live: defw 0
pp_out:  defw 0
pp_n:    defb 0
pp_sx:   defb 0
