; Tank Duel: the obstacle loops' quick test, in assembly (port: the C loops
; in td_core.c spent most of a tick rejecting obstacles in 32-bit C).
;
; uint8_t obs_next(void): the first obstacle k >= on_from (of on_n at on_ob)
; that is standing (hp > 0) and has |on_x - x| < reach and |on_z - z| <
; reach, reach = (r + on_extra) << 16 (16.16); 0xFF if none. Exactly the
; obstacles the C goes on to look at: every one it skips fails this.
;
; Obstacle (td.h, 16-bit int): +0 kind, +2 hp, +4 x, +8 z (16.16), +12 r

SECTION code_user

PUBLIC _obs_next, _on_ob, _on_n, _on_from, _on_x, _on_z, _on_extra

_obs_next:
    ld a,(_on_from)
    ld c,a                      ; C = k
    ld b,0
    ld hl,(_on_ob)
    ; HL += k * 14
    or a
    jr z,on_at
    ld de,14
on_skip:
    add hl,de
    dec a
    jr nz,on_skip
on_at:
on_loop:
    ld a,(_on_n)
    cp c
    jr z,on_none
    jr c,on_none
    push hl
    ; hp > 0 (int16)
    inc hl
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl                      ; HL = &x
    bit 7,d
    jr nz,on_no
    ld a,d
    or e
    jr z,on_no
    ; reach (whole units) = r + extra
    push hl
    ld de,8
    add hl,de                   ; &r
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld hl,(_on_extra)
    add hl,de
    ld (on_reach),hl
    pop hl
    ld de,_on_x
    call absdiff_hi             ; HL += 4; DE = |on_x - x| high word (or 0xFFFF if >= 2^31)
    call cmp_reach
    jr nc,on_no
    ld de,_on_z
    call absdiff_hi
    call cmp_reach
    jr nc,on_no
    pop hl
    ld l,c
    ld h,0
    ret
on_no:
    pop hl
    ld de,14
    add hl,de
    inc c
    jr on_loop
on_none:
    ld hl,0xFF
    ret

; carry set if DE (unsigned) < on_reach (signed, assumed >= 0 here; a
; negative reach is never > anything)
cmp_reach:
    push hl
    ld hl,(on_reach)
    bit 7,h
    jr nz,cr_no
    ex de,hl                    ; HL = |d| high, DE = reach
    or a
    sbc hl,de                   ; carry: |d| < reach
    pop hl
    ret
cr_no:
    pop hl
    or a
    ret

; DE -> the int32 v (on_x / on_z), HL -> the obstacle's int32 w: DE = the
; high word of |v - w| (the whole part); HL advanced by 4
absdiff_hi:
    ld a,(de)
    sub (hl)
    ld (on_t),a
    inc hl
    inc de
    ld a,(de)
    sbc a,(hl)
    ld (on_t + 1),a
    inc hl
    inc de
    ld a,(de)
    sbc a,(hl)
    ld (on_t + 2),a
    inc hl
    inc de
    ld a,(de)
    sbc a,(hl)
    ld (on_t + 3),a
    inc hl
    bit 7,a
    jr z,ad_pos
    push hl                     ; negative: DE = the high word of -(v - w)
    ld hl,(on_t)
    ld de,(on_t + 2)
    xor a
    sub l
    ld a,0
    sbc a,h
    ld a,0
    sbc a,e
    ld e,a
    ld a,0
    sbc a,d
    ld d,a
    pop hl
    ret
ad_pos:
    ld de,(on_t + 2)
    ret

SECTION bss_user
_on_ob:    defw 0
_on_n:     defb 0
_on_from:  defb 0
_on_x:     defs 4
_on_z:     defs 4
_on_extra: defw 0
on_reach:  defw 0
on_t:      defs 4

SECTION code_user

; fix fdiv(fix a, fix b) (td_fix.h): (a * 65536) / b truncated towards 0,
; cut to 32 bits: a 48 by 32 bit restoring division. port: the C's loop
; in assembly, the same result.
;   main: HL = remainder low, DE:BC = dividend shifting out / quotient in,
;         IX = divisor low;  alternate: HL' = remainder high, DE' = divisor high
PUBLIC _fdiv
_fdiv:
    push ix
    ld hl,4
    add hl,sp
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE:BC = a
    inc hl
    ld a,d
    ld (fd_sign),a
    push hl
    bit 7,d
    call nz,fd_neg_debc         ; DE:BC = |a|
    pop hl
    push de
    push bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE:BC = b
    ld a,(fd_sign)
    xor d
    ld (fd_sign),a
    bit 7,d
    call nz,fd_neg_debc         ; |b|
    push bc
    pop ix                      ; IX = divisor low
    push de
    exx
    pop de                      ; DE' = divisor high
    ld hl,0                     ; HL' = remainder high
    exx
    pop bc
    pop de                      ; DE:BC = |a|
    ld hl,0                     ; HL = remainder low
    ld a,48
fd_loop:
    ex af,af'
    sla c                       ; dividend << 1 (zeros after |a|'s 32 bits)
    rl b
    rl e
    rl d
    adc hl,hl                   ; remainder = remainder << 1 | the bit
    exx
    adc hl,hl
    exx
    ; remainder - divisor
    push hl
    ld a,l
    sub ixl
    ld l,a
    ld a,h
    sbc a,ixh
    ld h,a
    exx
    push hl
    sbc hl,de
    exx
    jr c,fd_back
    pop af                      ; keep the new remainder (drop the saved high)
    pop af                      ; (and low)
    inc c                       ; quotient bit 1
    jr fd_next
fd_back:
    exx
    pop hl
    exx
    pop hl
fd_next:
    ex af,af'
    dec a
    jr nz,fd_loop
    ld a,(fd_sign)
    bit 7,a
    call nz,fd_neg_debc
    ld h,b                      ; result DEHL: HL = the low word, DE = the high
    ld l,c
    pop ix
    ret

; DE:BC = -DE:BC
fd_neg_debc:
    xor a
    sub c
    ld c,a
    ld a,0
    sbc a,b
    ld b,a
    ld a,0
    sbc a,e
    ld e,a
    ld a,0
    sbc a,d
    ld d,a
    ret

SECTION bss_user
fd_sign:   defb 0

SECTION code_user

; fix fhypot(fix a, fix b) (td_fix.h, the original's a3d_fix.c): |a| and |b|
; halved together until both are below 46000, then floor(sqrt(a^2 + b^2))
; shifted back up. port: the C's loops in assembly, the same result.
PUBLIC _fhypot
EXTERN _isqrt32_fast
_fhypot:
    ld hl,2
    add hl,sp
    ld de,fh_x
    call fh_abs
    ld de,fh_y
    call fh_abs
    xor a
    ld (fh_sh),a
fh_loop:
    ld hl,(fh_x + 2)
    ld de,(fh_y + 2)
    ld a,h
    or d
    or l
    or e
    jr nz,fh_shift
    ld hl,(fh_x)
    ld de,(fh_y)
    ld a,h
    or d
    ld b,a
    ld a,l
    or e
    ld c,a                      ; BC = x | y
    ld hl,45999
    or a
    sbc hl,bc
    jr nc,fh_go                 ; below 46000
fh_shift:
    ld hl,fh_x + 3
    srl (hl)
    dec hl
    rr (hl)
    dec hl
    rr (hl)
    dec hl
    rr (hl)
    ld hl,fh_y + 3
    srl (hl)
    dec hl
    rr (hl)
    dec hl
    rr (hl)
    dec hl
    rr (hl)
    ld hl,fh_sh
    inc (hl)
    jr fh_loop
fh_go:
    ld hl,(fh_x)
    call sq16                   ; DE:HL = x^2
    ld (fh_s),hl
    ld (fh_s + 2),de
    ld hl,(fh_y)
    call sq16
    ld bc,(fh_s)
    add hl,bc
    ex de,hl
    ld bc,(fh_s + 2)
    adc hl,bc                   ; HL:DE = the sum
    push hl                     ; isqrt32(uint32): high word pushed first
    push de
    call _isqrt32_fast          ; HL = the root
    pop de
    pop de
    ld de,0
    ld a,(fh_sh)
    or a
    ret z
fh_up:
    add hl,hl
    rl e
    rl d
    dec a
    jr nz,fh_up
    ret                         ; DEHL

; the int32 at HL -> |it| at DE; HL advanced by 4
fh_abs:
    push de
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    bit 7,d
    jr z,fa_pos
    push hl
    call fd_neg_debc
    pop hl
fa_pos:
    ex (sp),hl                  ; HL = destination
    ld (hl),c
    inc hl
    ld (hl),b
    inc hl
    ld (hl),e
    inc hl
    ld (hl),d
    pop hl
    ret

; DE:HL = HL * HL (unsigned 16 bits)
sq16:
    ld b,h
    ld c,l
    ld d,l
    ld e,l
    mul d,e                     ; l l
    push de
    ld d,b
    ld e,c
    mul d,e                     ; h l
    ex de,hl
    add hl,hl                   ; 2 h l (17 bits)
    ld a,0
    adc a,a
    ex de,hl                    ; DE = 2 h l low 16, A = its bit 16
    pop hl                      ; HL = l l
    ld c,a
    ld a,h
    add a,e
    ld h,a
    ld a,d
    adc a,0
    ld e,a
    ld a,c
    adc a,0
    ld d,a                      ; DE:HL = l l + 2 h l << 8
    push hl
    ld h,b
    ld l,b
    ex de,hl                    ; HL = (bits 31..16 so far), DE = h h operands
    push hl
    mul d,e                     ; h h
    pop hl
    add hl,de
    ex de,hl
    pop hl
    ret

SECTION bss_user
fh_x:  defs 4
fh_y:  defs 4
fh_s:  defs 4
fh_sh: defb 0
