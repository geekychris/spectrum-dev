; The rules' hot loops in Z80N asm (game.c; port: the C, 32-bit sums through
; struct pointers, took ~2,000 T-states a statement):
;
;   uint16_t ap_near(const Vec3 *a, const Vec3 *b)
;       dist2_3d(a, b) when each axis' (d >> 4) is within -15..15 (the sum of
;       their squares), else 0xFFFF: the same answer to dist2_3d(a, b) < r for
;       any r <= 676 (the game's hit tests use 1..9)
;   void ap_move_bullets(Bullet *b, uint8_t n)
;       pos += v; life--, off at 0; off when y < -10 or y > MAX_ALT + 100
;   void ap_move_particles(Particle *p, uint8_t n)
;       pos += v; vy--; y < GROUND_Y (0): y = 0; life--, off at 0
;
; Bullet: pos x y z, vx vy vz (int32), life owner active (int16) = 30 bytes
; Particle: pos x y z, vx vy vz (int32), life active (int16) = 28 bytes

SECTION code_user

PUBLIC _ap_near, _ap_move_bullets, _ap_move_particles

defc MAX_ALT = 800

_ap_near:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = a
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a                      ; HL = b
    xor a
    ld (nr_sum),a
    ld (nr_sum + 1),a
    ld b,3
nr_axis:
    push bc
    ; q = (a - b) >> 4, 32-bit
    ld a,(de)
    sub (hl)
    ld c,a
    inc de
    inc hl
    ld a,(de)
    sbc a,(hl)
    ld b,a
    inc de
    inc hl
    ld a,(de)
    sbc a,(hl)
    ld (nr_t),a
    inc de
    inc hl
    ld a,(de)
    sbc a,(hl)                  ; A:(nr_t):B:C = d
    inc de
    inc hl
    push hl
    push de
    ; d >> 4 must be -15..15: d in -240..255, i.e. the top 3 bytes all 0 or all
    ; 0xFF with C's sign... check: (d + 240) unsigned < 496
    ld h,a
    ld a,(nr_t)
    ld l,a                      ; H:L:B:C = d
    ld a,c
    add a,240
    ld c,a
    ld a,b
    adc a,0
    ld b,a
    ld a,l
    adc a,0
    ld l,a
    ld a,h
    adc a,0
    or l
    jr nz,nr_far                ; d + 240 >= 65536 (or negative)
    ; BC = d + 240 (0..65535): need < 496
    ld a,b
    cp 1
    jr c,nr_ok                  ; < 256
    jr nz,nr_far
    ld a,c
    cp 496 - 256
    jr nc,nr_far
nr_ok:
    ; q = (d + 240) >> 4 - 15, |q| <= 15
    ld a,c
    srl b
    rra
    srl b
    rra
    srl b
    rra
    srl b
    rra
    sub 15                      ; q
    jp p,nr_pos
    neg
nr_pos:
    ld d,a
    ld e,a
    mul d,e
    ld hl,(nr_sum)
    add hl,de
    ld (nr_sum),hl
    pop de
    pop hl
    pop bc
    djnz nr_axis
    ld hl,(nr_sum)
    ret
nr_far:
    pop de
    pop hl
    pop bc
    ld hl,0xFFFF
    ret

; void ap_move_bullets(Bullet *b, uint8_t n)
_ap_move_bullets:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    or a
    ret z
    push ix
    push de
    pop ix
    ld b,a
mb_one:
    ld a,(ix+28)
    or (ix+29)
    jr z,mb_next                ; not active
    push bc
    call add_velocity
    ld l,(ix+24)                ; life--
    ld h,(ix+25)
    dec hl
    ld (ix+24),l
    ld (ix+25),h
    bit 7,h
    jr nz,mb_off
    ld a,h
    or l
    jr z,mb_off
mb_y:
    ; y < -10 or y > MAX_ALT + 100: off
    ld l,(ix+4)
    ld h,(ix+5)
    ld e,(ix+6)
    ld d,(ix+7)                 ; DE:HL = y
    bit 7,d
    jr nz,mb_neg
    ld a,d
    or e
    jr nz,mb_off_y              ; >= 65536
    push hl
    ld bc,MAX_ALT + 101
    or a
    sbc hl,bc
    pop hl
    jr nc,mb_off_y              ; y >= 901
    jr mb_done
mb_neg:
    ld a,d
    and e
    inc a
    jr nz,mb_off_y              ; below -65536
    ld bc,-10
    or a
    sbc hl,bc                   ; HL - (-10): y < -10 when (unsigned) HL < 0xFFF6
    jr nc,mb_done
mb_off_y:
mb_off:
    xor a
    ld (ix+28),a
    ld (ix+29),a
mb_done:
    pop bc
mb_next:
    ld de,30
    add ix,de
    djnz mb_one
    pop ix
    ret

; void ap_move_particles(Particle *p, uint8_t n)
_ap_move_particles:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    or a
    ret z
    push ix
    push de
    pop ix
    ld b,a
mp_one:
    ld a,(ix+26)
    or (ix+27)
    jr z,mp_next
    push bc
    call add_velocity
    ; vy-- (32-bit)
    ld a,(ix+16)
    sub 1
    ld (ix+16),a
    ld a,(ix+17)
    sbc a,0
    ld (ix+17),a
    ld a,(ix+18)
    sbc a,0
    ld (ix+18),a
    ld a,(ix+19)
    sbc a,0
    ld (ix+19),a
    ; y < 0: y = 0
    bit 7,(ix+7)
    jr z,mp_life
    xor a
    ld (ix+4),a
    ld (ix+5),a
    ld (ix+6),a
    ld (ix+7),a
mp_life:
    ld l,(ix+24)
    ld h,(ix+25)
    dec hl
    ld (ix+24),l
    ld (ix+25),h
    bit 7,h
    jr nz,mp_off
    ld a,h
    or l
    jr nz,mp_done
mp_off:
    xor a
    ld (ix+26),a
    ld (ix+27),a
mp_done:
    pop bc
mp_next:
    ld de,28
    add ix,de
    djnz mp_one
    pop ix
    ret

; pos (ix+0..11) += v (ix+12..23), three int32
add_velocity:
    push ix
    pop hl                      ; HL -> pos
    ld de,12
    add hl,de
    ex de,hl                    ; DE -> v
    push ix
    pop hl
    ld c,3
av_axis:
    ld b,4
    or a
av_byte:
    ld a,(de)
    adc a,(hl)
    ld (hl),a
    inc hl
    inc de
    djnz av_byte
    dec c
    jr nz,av_axis
    ret

SECTION bss_user
nr_sum: defs 2
nr_t:   defs 1
