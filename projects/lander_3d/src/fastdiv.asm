; fastdiv.asm - int32_t div100(int32_t x) __z88dk_fastcall: x / 100 truncated
; toward zero, as C's (the rules divide by their 100 steps a second about 15
; times a step; z88dk's 32-bit division is ~1,500 T-states, this ~450).
;
; |x| one byte at a time, high first: with r the remainder so far (< 100),
; (r * 256 + b) / 100 = qa[r] + (ra[r] + b) / 100, where r * 256 = 100 qa[r]
; + ra[r]; ra + b < 355, so the second part is 0..3, found by comparing.
; Resident (the rules in bank 20 call it).

SECTION code_user

PUBLIC _div100

_div100:                        ; DEHL = x
    bit 7,d
    jr z,dv_pos
    call dv_neg                 ; |x|
    call dv_u
dv_neg:                         ; DEHL = -DEHL
    xor a
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
dv_pos:
dv_u:                           ; DEHL = DEHL / 100 (unsigned)
    ld c,0                      ; r
    ld a,d
    call dv_byte
    ld d,a
    ld a,e
    call dv_byte
    ld e,a
    ld a,h
    call dv_byte
    ld h,a
    ld a,l
    call dv_byte
    ld l,a
    ret

; A = the next byte b, C = r: A = (r * 256 + b) / 100, C = its remainder
dv_byte:
    push hl
    ld b,a                      ; b
    ld h,dv_ra / 256
    ld l,c
    ld a,(hl)                   ; ra[r]
    inc h
    ld c,(hl)                   ; qa[r] (dv_qa = dv_ra + 256)
    add a,b                     ; t = ra + b (carry: >= 256)
    jr c,dv_big
    cp 200
    jr nc,dv_2
    cp 100
    jr nc,dv_1
    ld b,c                      ; t < 100: + 0
    ld c,a
    ld a,b
    pop hl
    ret
dv_1:
    sub 100
    inc c
    ld b,c
    ld c,a
    ld a,b
    pop hl
    ret
dv_big:                         ; t = 256 + A
    cp 44
    jr nc,dv_3
    add a,56                    ; t - 200
    inc c
    inc c
    ld b,c
    ld c,a
    ld a,b
    pop hl
    ret
dv_2:
    sub 200
    inc c
    inc c
    ld b,c
    ld c,a
    ld a,b
    pop hl
    ret
dv_3:
    sub 44                      ; t - 300
    inc c
    inc c
    inc c
    ld b,c
    ld c,a
    ld a,b
    pop hl
    ret

SECTION rodata_align_256
ALIGN 256
; ra[r] = r * 256 mod 100, then qa[r] = r * 256 / 100, r = 0..99
dv_ra:
    DEFB 0,56,12,68,24,80,36,92,48,4,60,16,72,28,84,40,96,52,8,64
    DEFB 20,76,32,88,44,0,56,12,68,24,80,36,92,48,4,60,16,72,28,84
    DEFB 40,96,52,8,64,20,76,32,88,44,0,56,12,68,24,80,36,92,48,4
    DEFB 60,16,72,28,84,40,96,52,8,64,20,76,32,88,44,0,56,12,68,24
    DEFB 80,36,92,48,4,60,16,72,28,84,40,96,52,8,64,20,76,32,88,44
    DEFS 156
dv_qa:
    DEFB 0,2,5,7,10,12,15,17,20,23,25,28,30,33,35,38,40,43,46,48
    DEFB 51,53,56,58,61,64,66,69,71,74,76,79,81,84,87,89,92,94,97,99
    DEFB 102,104,107,110,112,115,117,120,122,125,128,130,133,135,138,140,143,145,148,151
    DEFB 153,156,158,161,163,166,168,171,174,176,179,181,184,186,189,192,194,197,199,202
    DEFB 204,207,209,212,215,217,220,222,225,227,230,232,235,238,240,243,245,248,250,253
