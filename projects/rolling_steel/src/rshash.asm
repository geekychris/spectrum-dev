; unsigned long rs_mix(unsigned long h, unsigned long w): (h ^ w) * 16777619,
; one FNV-1a step over a 32-bit word (rshash.h). 16777619 = 2^24 + 403.
SECTION code_user
PUBLIC _rs_mix
EXTERN m_mul32s16

_rs_mix:
    ld hl,2
    add hl,sp
    ld c,(hl)
    inc hl
    ld b,(hl)
    inc hl
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DEBC = h
    inc hl
    ld a,(hl)
    xor c
    ld c,a
    inc hl
    ld a,(hl)
    xor b
    ld b,a
    inc hl
    ld a,(hl)
    xor e
    ld e,a
    inc hl
    ld a,(hl)
    xor d
    ld d,a                      ; DEBC = x = h ^ w
    ld a,c
    ld (rm_lo),a                ; x << 24 adds its low byte to the top byte
    ld h,b
    ld l,c
    ld bc,403
    call m_mul32s16             ; DEHL = x * 403
    ld a,(rm_lo)
    add a,d
    ld d,a
    ret

SECTION bss_user
rm_lo: defs 1
