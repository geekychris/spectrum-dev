; unsigned long rsmath_selftest(uint16_t n): runs rsmath.asm's routines on n
; pseudo-random operand pairs (xorshift32, magnitudes spread by random
; shifts) and returns an FNV-1a hash of every result. tools/test_rsmath.py
; computes the same with C's 32-bit semantics (test.py compares them).
SECTION code_user
PUBLIC _rsmath_selftest
EXTERN m_sra4, m_sra12, m_sla4, m_sla12, m_sra32, m_sla32, m_mul32x, m_mul32s16, m_smul16, m_div32, m_isqrt, _rs_mix

_rsmath_selftest:
    pop bc
    pop hl
    push hl
    push bc
    ld (mt_n),hl
    ld hl,0x9DC5
    ld (mt_h),hl
    ld hl,0x811C
    ld (mt_h + 2),hl            ; h = 2166136261
    ld hl,0x2545
    ld (mt_s),hl
    ld hl,0x1234
    ld (mt_s + 2),hl
mt_loop:
    call mt_rand
    ld (mt_a),hl
    ld (mt_a + 2),de
    call mt_rand
    ld (mt_b),hl
    ld (mt_b + 2),de
    call mt_rand                ; shifts: a >>= r & 31, b >>= (r >> 8) & 31 (arithmetic)
    ld (mt_r),hl
    ld a,l
    and 31
    ld b,a
    ld hl,(mt_a)
    ld de,(mt_a + 2)
    call m_sra32
    ld (mt_a),hl
    ld (mt_a + 2),de
    ld a,(mt_r + 1)
    and 31
    ld b,a
    ld hl,(mt_b)
    ld de,(mt_b + 2)
    call m_sra32
    ld (mt_b),hl
    ld (mt_b + 2),de
    ; a * b
    ld hl,(mt_a)
    ld de,(mt_a + 2)
    ld bc,mt_b
    call m_mul32x
    call mt_mix
    ; a * (int16)b
    ld hl,(mt_a)
    ld de,(mt_a + 2)
    ld bc,(mt_b)
    call m_mul32s16
    call mt_mix
    ; (int16)a * (int16)b
    ld hl,(mt_a)
    ld de,(mt_b)
    call m_smul16
    call mt_mix
    ; a / b (b != 0)
    ld hl,(mt_b)
    ld a,h
    or l
    ld hl,(mt_b + 2)
    or h
    or l
    jr z,mt_nodiv
    ld hl,(mt_a)
    ld de,(mt_a + 2)
    ld bc,mt_b
    call m_div32
    call mt_mix
mt_nodiv:
    ; a >> (b & 31), a << (b & 31)
    ld a,(mt_b)
    and 31
    ld b,a
    ld hl,(mt_a)
    ld de,(mt_a + 2)
    call m_sra32
    call mt_mix
    ld a,(mt_b)
    and 31
    ld b,a
    ld hl,(mt_a)
    ld de,(mt_a + 2)
    call m_sla32
    call mt_mix
    ; the fixed shifts: a >> 4, a >> 12, a << 4, a << 12
    ld hl,(mt_a)
    ld de,(mt_a + 2)
    call m_sra4
    call mt_mix
    ld hl,(mt_a)
    ld de,(mt_a + 2)
    call m_sra12
    call mt_mix
    ld hl,(mt_a)
    ld de,(mt_a + 2)
    call m_sla4
    call mt_mix
    ld hl,(mt_a)
    ld de,(mt_a + 2)
    call m_sla12
    call mt_mix
    ; isqrt((unsigned)a)
    ld hl,(mt_a)
    ld de,(mt_a + 2)
    call m_isqrt
    call mt_mix
    ld hl,(mt_n)
    dec hl
    ld (mt_n),hl
    ld a,h
    or l
    jp nz,mt_loop
    ld hl,(mt_h)
    ld de,(mt_h + 2)
    ret

; h = rs_mix(h, DEHL)
mt_mix:
    push de
    push hl
    ld hl,(mt_h + 2)
    push hl
    ld hl,(mt_h)
    push hl
    call _rs_mix
    pop bc
    pop bc
    pop bc
    pop bc
    ld (mt_h),hl
    ld (mt_h + 2),de
    ret

; DEHL = the next xorshift32: s ^= s << 13; s ^= s >> 17; s ^= s << 5
mt_rand:
    ld hl,(mt_s)
    ld de,(mt_s + 2)
    ld b,13
    call m_sla32
    call mt_xs
    ld b,17
    call mt_srl
    call mt_xs
    ld hl,(mt_s)
    ld de,(mt_s + 2)
    ld b,5
    call m_sla32
    call mt_xs
    ld hl,(mt_s)
    ld de,(mt_s + 2)
    ret
mt_xs:
    ld a,(mt_s)
    xor l
    ld (mt_s),a
    ld a,(mt_s + 1)
    xor h
    ld (mt_s + 1),a
    ld a,(mt_s + 2)
    xor e
    ld (mt_s + 2),a
    ld a,(mt_s + 3)
    xor d
    ld (mt_s + 3),a
    ret
; DEHL = s >>> 17 (logical)
mt_srl:
    ld hl,(mt_s + 2)            ; s >> 16
    srl h
    rr l
    ld de,0
    ret

SECTION bss_user
mt_n: defs 2
mt_h: defs 4
mt_s: defs 4
mt_a: defs 4
mt_b: defs 4
mt_r: defs 2
