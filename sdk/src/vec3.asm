; vec3.h on the Z80: Q14 vectors, every product 32 bits through fixmath's
; signed multiply (fixmath_smul: MUL on the Z80N), sums in 32 bits, then
; >> 14. Scratch memory is static (not re-entrant). Keeps IX and IY.

SECTION code_user

PUBLIC _v3_dot, _v3_dot32, _v3_cross, _v3_scale, _v3_mat, _v3_norm
EXTERN fixmath_smul, _isqrt32_fast

; ---------------------------------------------------------------- helpers

; DEHL >> 14 -> HL (the low 16 bits of the shifted value)
shr14:
    add hl,hl
    rl e
    rl d
    add hl,hl
    rl e
    rl d
    ex de,hl
    ret

; DEHL = sum of the products of the vectors at (va) and (vb), which move on
dot_core:
    ld hl,0
    ld (acc),hl
    ld (acc+2),hl
    ld a,3
dot_loop:
    push af
    ld hl,(va)
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (va),hl
    ld hl,(vb)
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld (vb),hl
    ld h,b
    ld l,c
    call fixmath_smul           ; DEHL = a.c * b.c
    call acc_add
    pop af
    dec a
    jr nz,dot_loop
    ld hl,(acc)
    ld de,(acc+2)
    ret

; acc += DEHL
acc_add:
    ld bc,(acc)
    add hl,bc
    ld (acc),hl
    ld hl,(acc+2)
    adc hl,de
    ld (acc+2),hl
    ret

; acc -= DEHL
acc_sub:
    ld b,h
    ld c,l
    ld hl,(acc)
    or a
    sbc hl,bc
    ld (acc),hl
    ld hl,(acc+2)
    sbc hl,de
    ld (acc+2),hl
    ret

; HL = arg n's address on the caller's stack (n = 0, 1, 2 ...): called
; directly from a C entry point, so the return address into C is at SP+2
args:
    ld hl,4
    add hl,sp
    ret

; DE = the 16-bit word at (HL), HL += 2
ld_de_hl:
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ret

; ---------------------------------------------------------------- dot

; int16_t v3_dot(const vec3 *a, const vec3 *b)
_v3_dot:
    call dot_args
    call dot_core
    jp shr14

; int32_t v3_dot32(const vec3 *a, const vec3 *b)
_v3_dot32:
    call dot_args
    jp dot_core

dot_args:
    ld hl,4
    add hl,sp
    call ld_de_hl
    ld (va),de
    call ld_de_hl
    ld (vb),de
    ret

; ---------------------------------------------------------------- cross

; void v3_cross(vec3 *r, const vec3 *a, const vec3 *b)
_v3_cross:
    call args
    call ld_de_hl
    ld (vr),de
    call ld_de_hl
    ld (va),de
    call ld_de_hl
    ld (vb),de
    ; x = ay bz - az by
    ld a,2
    ld b,4
    ld c,4
    ld d,2
    call cross_term
    ld (tmp),hl
    ; y = az bx - ax bz
    ld a,4
    ld b,0
    ld c,0
    ld d,4
    call cross_term
    ld (tmp+2),hl
    ; z = ax by - ay bx
    ld a,0
    ld b,2
    ld c,2
    ld d,0
    call cross_term
    ld (tmp+4),hl
    jp store_tmp

; HL = (a[A] b[B] - a[C] b[D]) >> 14, offsets in bytes
cross_term:
    push bc
    push de
    call prod                   ; a[A] * b[B]
    ld (acc),hl
    ld (acc+2),de
    pop de
    pop bc
    ld a,c
    ld b,d
    call prod                   ; a[C] * b[D]
    call acc_sub
    ld hl,(acc)
    ld de,(acc+2)
    jp shr14

; DEHL = a[A] * b[B] (byte offsets)
prod:
    ld hl,(va)
    call add_hl_a
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld hl,(vb)
    ld a,b
    call add_hl_a
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    jp fixmath_smul

; HL += A (A unsigned)
add_hl_a:
    add a,l
    ld l,a
    ret nc
    inc h
    ret

; *(vr) = tmp
store_tmp:
    ld hl,tmp
    ld de,(vr)
    ld bc,6
    ldir
    ret

; ---------------------------------------------------------------- scale

; void v3_scale(vec3 *r, const vec3 *a, int16_t s)
_v3_scale:
    call args
    call ld_de_hl
    ld (vr),de
    call ld_de_hl
    ld (va),de
    call ld_de_hl
    ld (vb),de                  ; vb = s itself
    ld hl,tmp
    ld (vt),hl
    ld a,3
scale_loop:
    push af
    ld hl,(va)
    call ld_de_hl
    ld (va),hl
    ld hl,(vb)
    call fixmath_smul
    call shr14
    call put_t
    pop af
    dec a
    jr nz,scale_loop
    jp store_tmp

; *(vt)++ = HL
put_t:
    ex de,hl
    ld hl,(vt)
    ld (hl),e
    inc hl
    ld (hl),d
    inc hl
    ld (vt),hl
    ret

; ---------------------------------------------------------------- matrix

; void v3_mat(vec3 *r, const vec3 *m, const vec3 *a)
_v3_mat:
    call args
    call ld_de_hl
    ld (vr),de
    call ld_de_hl
    ld (vm),de
    call ld_de_hl
    ld (vx),de                  ; the vector
    ld hl,tmp
    ld (vt),hl
    ld a,3
mat_loop:
    push af
    ld hl,(vm)
    ld (va),hl
    ld hl,(vx)
    ld (vb),hl
    call dot_core
    call shr14
    call put_t
    ld hl,(va)                  ; the next row
    ld (vm),hl
    pop af
    dec a
    jr nz,mat_loop
    jp store_tmp

; ---------------------------------------------------------------- normalise

; void v3_norm(vec3 *r, const vec3 *a)
_v3_norm:
    call args
    call ld_de_hl
    ld (vr),de
    call ld_de_hl
    ex de,hl
    ld de,tmp
    ld bc,6
    ldir                        ; tmp = a
    ; while any |c| > 30000: halve all (arithmetic shift)
norm_big:
    ld hl,tmp
    ld b,3
norm_big_chk:
    call ld_de_hl
    ex de,hl
    bit 7,h
    jr z,norm_big_pos
    ; negative: c < -30000 ?  c + 30000 < 0
    push de
    ld de,30000
    add hl,de
    pop de
    jr nc,norm_halve            ; no carry: c + 30000 < 0 (c >= -32768)
    jr norm_big_next
norm_big_pos:
    push de
    ld de,30001
    or a
    sbc hl,de
    pop de
    jr nc,norm_halve            ; c >= 30001
norm_big_next:
    ex de,hl
    djnz norm_big_chk
    jr norm_small
norm_halve:
    ld hl,tmp
    ld b,3
norm_halve_loop:
    inc hl
    sra (hl)
    dec hl
    rr (hl)
    inc hl
    inc hl
    djnz norm_halve_loop
    jr norm_big

    ; while all |c| < 4096 and not all zero and fewer than 8 doublings: double
norm_small:
    ld a,8
norm_small_loop:
    push af
    ld hl,(tmp)
    ld de,(tmp+2)
    ld a,h
    or l
    or d
    or e
    ld bc,(tmp+4)
    or b
    or c
    jr z,norm_small_done        ; all zero
    call small16
    jr nc,norm_small_done
    ex de,hl
    call small16
    jr nc,norm_small_done
    ld h,b
    ld l,c
    call small16
    jr nc,norm_small_done
    ld hl,tmp
    ld b,3
norm_dbl:
    sla (hl)
    inc hl
    rl (hl)
    inc hl
    djnz norm_dbl
    pop af
    dec a
    jr nz,norm_small_loop
    push af
norm_small_done:
    pop af
    ; len = isqrt32(x^2 + y^2 + z^2)
    ld hl,tmp
    ld (va),hl
    ld (vb),hl
    call dot_core               ; DEHL = the sum (fits 32 bits unsigned)
    push de
    push hl
    call _isqrt32_fast
    pop af
    pop af
    ld a,h
    or l
    jr nz,norm_len
    ld hl,0                     ; zero: (0, 16384, 0)
    ld (tmp),hl
    ld (tmp+4),hl
    ld h,0x40
    ld (tmp+2),hl
    jp store_tmp
norm_len:
    ld b,h
    ld c,l                      ; BC = len
    ; inv = 2^28 / len = (0x1000 << 16 | 0) / len: high word, then low
    ld hl,0x1000
    or a
    sbc hl,bc
    jr nc,norm_div_hi           ; len <= 0x1000: the high word isn't 0
    ld hl,0                     ; len > 0x1000 (the usual): high word 0,
    ld (inv+2),hl               ; remainder 0x1000
    ld h,0x10
    jr norm_div_lo
norm_div_hi:
    ld hl,0
    ld de,0x1000
    call div32
    ld (inv+2),de               ; quotient's high word
norm_div_lo:
    ld de,0
    call div32                  ; HL = remainder < len
    ld (inv),de
    ; each component: (c * inv) >> 14, inv = hi:lo (both unsigned)
    ld hl,tmp
    ld (va),hl
    ld (vt),hl
    ld a,3
norm_mul:
    push af
    ld hl,(va)
    call ld_de_hl
    ld (va),hl
    push de                     ; c
    ld hl,(inv)
    call fixmath_smul           ; DEHL = c * lo (lo taken as signed)
    ld (acc),hl
    ld (acc+2),de
    pop de                      ; c
    ld hl,(inv)
    bit 7,h
    jr z,norm_lo_pos
    ld hl,(acc+2)               ; lo >= 32768: + c << 16
    add hl,de
    ld (acc+2),hl
norm_lo_pos:
    ld hl,(inv+2)
    ld a,h
    or l
    jr z,norm_hi_zero
    call fixmath_smul           ; c * hi: its low word << 16
    ld de,(acc+2)
    add hl,de
    ld (acc+2),hl
norm_hi_zero:
    ld hl,(acc)
    ld de,(acc+2)
    call shr14
    call put_t
    pop af
    dec a
    jr nz,norm_mul
    jp store_tmp

; carry set if -4096 < HL < 4096
small16:
    push hl
    push de
    ld de,4096
    add hl,de                   ; HL + 4096 in 1..8191 (unsigned, < 8192)
    ld a,h
    pop de
    pop hl
    cp 0x20                     ; < 0x2000 ?
    ret nc
    push hl
    push de
    ld de,4096
    add hl,de
    ld a,h
    or l
    pop de
    pop hl
    jr z,small_no               ; HL = -4096
    scf
    ret
small_no:
    or a
    ret

; DE = (HL:DE) / BC, HL = the remainder; needs HL < BC
div32:
    ld a,16
div32_loop:
    ex de,hl
    add hl,hl
    ex de,hl
    adc hl,hl
    jr c,div32_over
    sbc hl,bc                   ; carry is clear here
    jr nc,div32_one
    add hl,bc
    jr div32_next
div32_over:
    or a
    sbc hl,bc
div32_one:
    inc e
div32_next:
    dec a
    jr nz,div32_loop
    ret

SECTION bss_user

acc: defs 4
va:  defs 2
vb:  defs 2
vr:  defs 2
vm:  defs 2
vx:  defs 2
vt:  defs 2
tmp: defs 6
inv: defs 4
