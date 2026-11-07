; fixmath.h's isqrt32_fast, in its own file so programs that don't call it
; (it has a 384-byte table) stay the same size

SECTION code_user

PUBLIC _isqrt32_fast
EXTERN l_mulu_32_16x16

; uint16_t isqrt32_fast(uint32_t v): floor(sqrt(v)), exactly, as isqrt32. A seed from the top
; 8 bits (after shifting v left by 2k until its top two bits aren't both 0:
; within 1/256 of the root), one Newton step r = (r + v / r) / 2 (a 32 / 16
; division), then r - 1 while r^2 > v, r + 1 while (r + 1)^2 <= v. About a
; third of the time of isqrt32's bit-by-bit loop, for 384 bytes of table.
_isqrt32_fast:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)                   ; BCDE = v
    ld (isq_v),de
    ld (isq_v + 2),bc
    ld a,b
    or c
    or d
    or e
    ld h,a
    ld l,a
    ret z                       ; sqrt(0) = 0
    ; normalise: k pairs of bits
    ld l,0                      ; k
isq_norm:
    ld a,b
    and 0xC0
    jr nz,isq_seed
    ld a,2
isq_sh:
    sla e
    rl d
    rl c
    rl b
    dec a
    jr nz,isq_sh
    inc l
    jr isq_norm
isq_seed:
    ld a,b                      ; the top byte, 64..255
    sub 64
    push hl                     ; k
    ld l,a
    ld h,0
    add hl,hl
    ld de,isq_seeds
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)                   ; DE = sqrt(v << 2k), within 1/256
    pop hl
    ld b,l                      ; k
    ld a,b
    or a
    jr z,isq_s2
isq_s1:
    srl d
    rr e
    djnz isq_s1
isq_s2:
    ld a,d
    or e
    jr nz,isq_s3
    inc e                       ; (at least 1)
isq_s3:
    ld (isq_r),de
    ; q = v / r (16 bits; 65535 if it wouldn't fit)
    ld b,d
    ld c,e                      ; BC = r
    ld hl,(isq_v + 2)
    or a
    sbc hl,bc
    ld de,0xFFFF
    jr nc,isq_q                 ; v_hi >= r
    add hl,bc
    ld de,(isq_v)
    ld a,16
isq_div:
    ex de,hl
    add hl,hl
    ex de,hl
    adc hl,hl
    jr c,isq_dover
    sbc hl,bc
    jr nc,isq_done1
    add hl,bc
    jr isq_dnext
isq_dover:
    or a
    sbc hl,bc
isq_done1:
    inc e
isq_dnext:
    dec a
    jr nz,isq_div
isq_q:
    ; r = (r + q) / 2 (17 bits)
    ld hl,(isq_r)
    add hl,de
    rr h
    rr l
    ; while r^2 > v: r--
isq_down:
    push hl
    call isq_sq                 ; carry if r^2 > v
    pop hl
    jr nc,isq_up
    dec hl
    jr isq_down
    ; while (r + 1)^2 <= v: r++
isq_up:
    inc hl
    ld a,h
    or l
    jr z,isq_max                ; r + 1 = 65536
    push hl
    call isq_sq
    pop hl
    jr nc,isq_up                ; (r + 1)^2 <= v: keep going
isq_max:
    dec hl
    ret

; carry if HL^2 > v (isq_v)
isq_sq:
    ld d,h
    ld e,l
    call l_mulu_32_16x16        ; DEHL = HL * DE
    ld bc,(isq_v + 2)
    ex de,hl                    ; HL = high, DE = low
    or a
    sbc hl,bc
    jr z,isq_lo
    ccf                         ; carry: high(r^2) > high(v)
    ret c
    scf
    ccf
    ret
isq_lo:
    ex de,hl
    ld bc,(isq_v)
    or a
    sbc hl,bc                   ; low(r^2) - low(v)
    jr z,isq_eq
    ccf
    ret
isq_eq:
    or a
    ret

isq_seeds:                      ; sqrt((t + 0.5) 2^24), t = 64..255
    defw 32896, 33150, 33402, 33652, 33900, 34147, 34392, 34635, 34876, 35116, 35354, 35590
    defw 35825, 36059, 36291, 36521, 36750, 36978, 37204, 37429, 37652, 37874, 38095, 38315
    defw 38533, 38750, 38966, 39181, 39394, 39606, 39818, 40028, 40237, 40445, 40652, 40857
    defw 41062, 41266, 41469, 41671, 41871, 42071, 42270, 42468, 42665, 42861, 43057, 43251
    defw 43445, 43637, 43829, 44020, 44210, 44400, 44588, 44776, 44963, 45149, 45334, 45519
    defw 45703, 45886, 46069, 46250, 46431, 46612, 46791, 46970, 47149, 47326, 47503, 47679
    defw 47855, 48030, 48204, 48378, 48551, 48723, 48895, 49067, 49237, 49407, 49577, 49746
    defw 49914, 50082, 50249, 50416, 50582, 50747, 50912, 51077, 51241, 51404, 51567, 51730
    defw 51892, 52053, 52214, 52374, 52534, 52694, 52853, 53011, 53169, 53327, 53484, 53640
    defw 53797, 53952, 54108, 54262, 54417, 54571, 54724, 54877, 55030, 55182, 55334, 55485
    defw 55636, 55787, 55937, 56087, 56236, 56385, 56534, 56682, 56830, 56977, 57124, 57271
    defw 57417, 57563, 57709, 57854, 57999, 58143, 58287, 58431, 58574, 58717, 58860, 59002
    defw 59144, 59286, 59427, 59568, 59709, 59849, 59989, 60129, 60268, 60407, 60546, 60684
    defw 60822, 60960, 61098, 61235, 61372, 61508, 61644, 61780, 61916, 62051, 62186, 62321
    defw 62456, 62590, 62724, 62857, 62991, 63124, 63256, 63389, 63521, 63653, 63785, 63916
    defw 64047, 64178, 64309, 64439, 64569, 64699, 64828, 64957, 65086, 65215, 65344, 65472

SECTION bss_user
isq_v: defs 4
isq_r: defs 2
