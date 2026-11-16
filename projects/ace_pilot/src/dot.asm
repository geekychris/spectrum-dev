; angle_to's fast path (game.c) in Z80N asm: the dot product
;   (dx * sin_tab[a] + dz * cos_tab[a]) >> 16 for |dx|, |dz| < 16384 (the sum
;   then fits 32 bits and the result 16; sin_tab / cos_tab are the original's
;   16.16 values, |v| <= 65536), and the search around an estimate.
;   port: in C a dot product took ~3,500 T-states.

SECTION code_user

PUBLIC _ap_angle, _ap_scan
EXTERN _sin_tab, _cos_tab, l_z80n_mulu_32_16x16

; uint8_t ap_angle(int16_t dx, int16_t dz, uint8_t lo)
;   angle_to's fast path (game.c): candidates lo, lo+4 .. lo+24 (mod 256) in
;   ascending order from 0, the first with the largest dot product; then its
;   neighbours -3..+3 in order (the original's refine pass; the candidate
;   itself can't beat itself). Dot products compared as int16, strict >.
_ap_angle:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (dot_dx),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (dot_dz),de
    ld a,(hl)                   ; lo
    ld (ang_lo),a
    ; the first candidate in ascending order: the first one past the wrap
    ld b,0                      ; B = k
    ld c,a
    add a,24
    jr nc,ang_nowrap            ; lo + 24 < 256: no wrap
    ld a,c                      ; k = (256 - lo) / 4
    neg
    rrca
    rrca
    and 0x3F
    ld b,a
ang_nowrap:
    ld a,b
    ld (ang_k),a
    ld hl,0                     ; best (biased: dot ^ 0x8000, compared unsigned); 0 = below everything
    ld (ang_best),hl
    xor a
    ld (ang_have),a
    ld a,7
    ld (ang_n),a
ang_coarse:
    ld a,(ang_k)                ; c = lo + k * 4 (k mod 7)
    add a,a
    add a,a
    ld b,a
    ld a,(ang_lo)
    add a,b
    call ang_try
    ld a,(ang_k)
    inc a
    cp 7
    jr c,ang_k_ok
    xor a
ang_k_ok:
    ld (ang_k),a
    ld hl,ang_n
    dec (hl)
    jr nz,ang_coarse
    ; refine: b-3, b-2, b-1, b+1, b+2, b+3
    ld a,(ang_res)
    ld (ang_b),a
    sub 3
    call ang_try
    ld a,(ang_b)
    sub 2
    call ang_try
    ld a,(ang_b)
    dec a
    call ang_try
    ld a,(ang_b)
    inc a
    call ang_try
    ld a,(ang_b)
    add a,2
    call ang_try
    ld a,(ang_b)
    add a,3
    call ang_try
    ld a,(ang_res)
    ld l,a
    ret

; uint8_t ap_scan(int16_t dx, int16_t dz)
;   the original's angle_to scan (game.c angle_to_scan) for short vectors
;   (|dx|, |dz| < 256: the dot products tie over wide ranges, so no shortcut):
;   every 4th angle from 0 in order, the first with the largest dot product,
;   then the refine loop as the C has it: from best - 3 until best + 4, best
;   being updated as it goes. port: in C the 71 dot products took ~1/5 of a
;   busy tick.
_ap_scan:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld (dot_dx),de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (dot_dz),de
    xor a
    ld (ang_have),a
    ld (ang_k),a                ; a = 0
sc_coarse:
    ld a,(ang_k)
    call ang_try
    ld a,(ang_k)
    add a,4
    ld (ang_k),a
    jr nz,sc_coarse             ; 252 + 4 wraps to 0: 64 tried
    ld a,(ang_res)
    sub 3
    ld (ang_k),a
sc_refine:
    ld a,(ang_res)
    add a,4
    ld b,a
    ld a,(ang_k)
    cp b
    jr z,sc_done                ; a == best + 4
    call ang_try
    ld a,(ang_k)
    inc a
    ld (ang_k),a
    jr sc_refine
sc_done:
    ld a,(ang_res)
    ld l,a
    ret

; dot at angle A; if larger than the best (or the first), A becomes the result
ang_try:
    ld (dot_a),a
    call dot_core               ; HL = dot
    ld a,h
    xor 0x80
    ld h,a                      ; biased
    ld a,(ang_have)
    or a
    jr z,ang_take
    ld de,(ang_best)
    or a
    sbc hl,de
    ret c
    ret z                       ; not larger
    add hl,de
ang_take:
    ld (ang_best),hl
    ld a,1
    ld (ang_have),a
    ld a,(dot_a)
    ld (ang_res),a
    ret

; HL = (dot_dx * sin_tab[dot_a] + dot_dz * cos_tab[dot_a]) >> 16
dot_core:
    ld de,(dot_dx)
    ld hl,_sin_tab
    call dot_term               ; DEHL = dx * sin_tab[a]
    ld (dot_sum),hl
    ld (dot_sum + 2),de
    ld de,(dot_dz)
    ld hl,_cos_tab
    call dot_term               ; DEHL = dz * cos_tab[a]
    ld bc,(dot_sum)
    add hl,bc
    ex de,hl
    ld bc,(dot_sum + 2)
    adc hl,bc                   ; HL = high word of the sum = sum >> 16
    ret

; DEHL = DE (int16) * table[dot_a] (int32 entry of HL's table, |v| <= 65536)
dot_term:
    ld a,(dot_a)
    ld c,a
    ld b,0
    add hl,bc
    add hl,bc
    add hl,bc
    add hl,bc                   ; HL -> entry
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BC = low word
    inc hl
    ld a,(hl)                   ; byte 2: 0 or 1 if positive, 0xFF or 0xFE if negative
    inc hl
    ld h,(hl)                   ; byte 3: sign
    ; sign of the product: sign(v) ^ sign(entry)
    ld l,a
    ld a,d
    xor h
    ld (dot_neg),a              ; bit 7 = negate the product
    ; |v|
    bit 7,d
    jr z,dt_vpos
    xor a
    sub e
    ld e,a
    sbc a,a
    sub d
    ld d,a
dt_vpos:
    ; |entry|: positive: L:BC; negative: negate the 32-bit H:L:B:C
    bit 7,h
    jr z,dt_epos
    xor a
    sub c
    ld c,a
    ld a,0
    sbc a,b
    ld b,a
    ld a,0
    sbc a,l
    ld l,a                      ; L = byte 2 of |entry| (0 or 1)
dt_epos:
    ld a,l
    or a
    jr z,dt_mul
    ld hl,0                     ; |entry| = 65536: |v| << 16 (DE = |v|, HL = 0)
    jr dt_sign
dt_mul:
    ld h,b
    ld l,c                      ; HL = |entry|, DE = |v|
    call l_z80n_mulu_32_16x16   ; DEHL = |v| * |entry|
dt_sign:
    ld a,(dot_neg)
    bit 7,a
    ret z
    xor a                       ; negate DEHL
    sub l
    ld l,a
    ld a,0
    sbc a,h
    ld h,a
    ld a,0
    sbc a,e
    ld e,a
    ld a,0
    sbc a,d
    ld d,a
    ret

SECTION bss_user
dot_a:   defs 1
dot_neg: defs 1
dot_sum: defs 4
dot_dx:  defs 2
dot_dz:  defs 2
ang_lo:  defs 1
ang_k:   defs 1
ang_n:   defs 1
ang_b:   defs 1
ang_res: defs 1
ang_have: defs 1
ang_best: defs 2
