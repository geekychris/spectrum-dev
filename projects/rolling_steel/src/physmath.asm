; The physics' small maths, resident: game.c and game_path.c call isin,
; icos, isqrt and vlenp too (phys.c's own functions, see phys.asm).
SECTION code_user
INCLUDE "rsmac.inc"
PUBLIC _isin, _icos, _isqrt, _vlenp
PUBLIC p_isin, p_isinl, p_icosl, p_sq, p_vlen, ld32_hl, sext
EXTERN m_sra4, m_sra12, m_sla4, m_sla12, m_sra32, m_sla32, m_smul16, m_mul32x, m_isqrt, qsin


; HL = isin(HL) (Q14, a: 65536 a turn); phys.c's table and interpolation
p_isin:
    ld c,l                      ; f = a & 255
    ld a,h
    and 63
    ld e,a                      ; i
    ld a,h
    rlca
    rlca
    and 3
    ld (is_q),a                 ; q
    rra
    jr c,is_odd
    ld a,e                      ; v0 = qsin[i], v1 = qsin[i + 1]
    add a,a
    ld l,a
    ld h,0
    ld de,qsin
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    jr is_mix
is_odd:
    ld a,64                     ; v0 = qsin[64 - i], v1 = qsin[63 - i]
    sub e
    add a,a
    ld l,a
    ld h,0
    ld de,qsin
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    dec hl
    dec hl
    ld a,(hl)
    dec hl
    ld l,(hl)
    ld h,a
is_mix:
    ld (is_v0),de
    or a
    sbc hl,de                   ; v1 - v0
    ld e,c
    ld d,0
    call m_smul16               ; (v1 - v0) * f
    ld l,h                      ; >> 8 (the result fits in 16 bits)
    ld h,e
    ld de,(is_v0)
    add hl,de                   ; v
    ld a,(is_q)
    and 2
    ret z
    xor a
    sub l
    ld l,a
    sbc a,a
    sub h
    ld h,a
    ret

; DEHL = (long)isin(HL), icos(HL)
p_isinl:
    call p_isin
sext:
    ld a,h
    rla
    sbc a,a
    ld d,a
    ld e,a
    ret
p_icosl:
    ld de,16384
    add hl,de
    jr p_isinl

; long isin(long a), long icos(long a)
_isin:
    ld hl,2
    add hl,sp
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    jr p_isinl
_icos:
    ld hl,2
    add hl,sp
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    jr p_icosl

; unsigned long isqrt(unsigned long v)
_isqrt:
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
    ex de,hl
    jp m_isqrt

; DEHL = DEHL * DEHL (low 32 bits)
p_sq:
    ST32 sq_t
    MUL32 sq_t
    ret

; fix vlen(V3 a) with the vector at (HL): isqrt of the sum of (c >> 4)^2, << 4
p_vlen:
    push hl
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ex de,hl
    SRA4
    call p_sq
    ST32 vl_s
    pop hl
    push hl
    ld bc,4
    add hl,bc
    call ld32_hl
    SRA4
    call p_sq
    ADD32 vl_s
    ST32 vl_s
    pop hl
    ld bc,8
    add hl,bc
    call ld32_hl
    SRA4
    call p_sq
    ADD32 vl_s
    call m_isqrt
    SLA4
    ret

; fix vlenp(const V3 *a)
_vlenp:
    pop bc
    pop hl
    push hl
    push bc
    jr p_vlen

; DEHL = the long at (HL)
ld32_hl:
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ex de,hl
    ret



SECTION bss_user
is_q:   defs 1
is_v0:  defs 2
sq_t:   defs 4
vl_s:   defs 4
