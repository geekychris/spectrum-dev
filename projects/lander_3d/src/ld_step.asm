; ld_step.asm - parts of ld_core.c's ld_step() in Z80N assembly, on its copy
; of the craft (ld_sc: p[3] v[3] pitch roll wp wr throttle rcs[2]); the same
; values as the C they replace (tools/check_core.sh flies the C; the game's
; self-test, these):
;
;   void ls_axes(const LdInput *in)   both axis() calls (the assist on, fuel
;                                     left): the rates toward the commands,
;                                     the thrusters' flags (cleared before)
;   void ls_angles(void)              pitch and roll += rate / 100, held
;                                     within +-MAX_TILT
;   void ls_integrate(const fix *a)   v += a / 100, then p += v / 100
;
; Bank 20 with the rules. IX, IY kept.

EXTERN _ld_sc, _fmul, _div100
PUBLIC _ls_axes, _ls_angles, _ls_integrate

SECTION bss_user
ls_in:  defw 0
ls_t:   defw 0, 0
ls_a:   defw 0

SECTION BANK_20

_ls_axes:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (ls_in),de
; ---- axis 0: ld_core.c axis() (the assist on, fuel left)
    ld hl,(ls_in)
    ld de,4
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ex de,hl                    ; DEHL = the command
    ld a,l
    sub 0x00
    ld a,h
    sbc a,0x00
    ld a,e
    sbc a,0xFF
    ld a,d
    sbc a,0xFF
    jp po,lt10
    xor 0x80
lt10:
    jp m,cl1
    ld a,l
    sub 0x01
    ld a,h
    sbc a,0x00
    ld a,e
    sbc a,0x01
    ld a,d
    sbc a,0x00
    jp po,lt11
    xor 0x80
lt11:
    jp m,co3
    ld hl,0x0000
    ld de,0x0001
    jr co3
cl1:
    ld hl,0x0000
    ld de,0xFFFF
co3:
    ld bc,0x0001
    push bc
    ld bc,0x1999
    push bc
    push de
    push hl
    call _fmul                  ; cmd RATE
    pop bc
    pop bc
    pop bc
    pop bc
    ld bc,(_ld_sc + 32)
    or a
    sbc hl,bc
    ex de,hl
    ld bc,(_ld_sc + 32 + 2)
    sbc hl,bc
    ex de,hl
    add hl,hl
    ex de,hl
    adc hl,hl
    ex de,hl
    ld (ls_t),hl
    ld (ls_t + 2),de
    add hl,hl
    ex de,hl
    adc hl,hl
    ex de,hl
    ld bc,(ls_t)
    add hl,bc
    ex de,hl
    ld bc,(ls_t + 2)
    adc hl,bc
    ex de,hl
                            ; a = err * 6 (mod 2^32, as C's)
    ld a,l
    sub 0x9A
    ld a,h
    sbc a,0x99
    ld a,e
    sbc a,0xFD
    ld a,d
    sbc a,0xFF
    jp po,lt12
    xor 0x80
lt12:
    jp m,al4
    ld a,l
    sub 0x67
    ld a,h
    sbc a,0x66
    ld a,e
    sbc a,0x02
    ld a,d
    sbc a,0x00
    jp po,lt13
    xor 0x80
lt13:
    jp m,ao6
    ld hl,0x6666
    ld de,0x0002
    jr ao6
al4:
    ld hl,0x999A
    ld de,0xFFFD
ao6:
    ld a,l
    sub 0xCD
    ld a,h
    sbc a,0xCC
    ld a,e
    sbc a,0xFF
    ld a,d
    sbc a,0xFF
    jp po,lt14
    xor 0x80
lt14:
    jp m,rm8
    ld a,l
    sub 0x34
    ld a,h
    sbc a,0x33
    ld a,e
    sbc a,0x00
    ld a,d
    sbc a,0x00
    jp po,lt15
    xor 0x80
lt15:
    jp m,rd9
    ld bc,1                     ; a > 0.2: +1
    jr rp7
rm8:
    ld bc,-1                    ; a < -0.2: -1
rp7:
    ld (_ld_sc + 44),bc
rd9:
    call _div100
    ld bc,(_ld_sc + 32)
    add hl,bc
    ex de,hl
    ld bc,(_ld_sc + 32 + 2)
    adc hl,bc
    ex de,hl
    ld (_ld_sc + 32),hl
    ld (_ld_sc + 32 + 2),de
; ---- axis 1: ld_core.c axis() (the assist on, fuel left)
    ld hl,(ls_in)
    ld de,8
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ex de,hl                    ; DEHL = the command
    ld a,l
    sub 0x00
    ld a,h
    sbc a,0x00
    ld a,e
    sbc a,0xFF
    ld a,d
    sbc a,0xFF
    jp po,lt25
    xor 0x80
lt25:
    jp m,cl16
    ld a,l
    sub 0x01
    ld a,h
    sbc a,0x00
    ld a,e
    sbc a,0x01
    ld a,d
    sbc a,0x00
    jp po,lt26
    xor 0x80
lt26:
    jp m,co18
    ld hl,0x0000
    ld de,0x0001
    jr co18
cl16:
    ld hl,0x0000
    ld de,0xFFFF
co18:
    ld bc,0x0001
    push bc
    ld bc,0x1999
    push bc
    push de
    push hl
    call _fmul                  ; cmd RATE
    pop bc
    pop bc
    pop bc
    pop bc
    ld bc,(_ld_sc + 36)
    or a
    sbc hl,bc
    ex de,hl
    ld bc,(_ld_sc + 36 + 2)
    sbc hl,bc
    ex de,hl
    add hl,hl
    ex de,hl
    adc hl,hl
    ex de,hl
    ld (ls_t),hl
    ld (ls_t + 2),de
    add hl,hl
    ex de,hl
    adc hl,hl
    ex de,hl
    ld bc,(ls_t)
    add hl,bc
    ex de,hl
    ld bc,(ls_t + 2)
    adc hl,bc
    ex de,hl
                            ; a = err * 6 (mod 2^32, as C's)
    ld a,l
    sub 0x9A
    ld a,h
    sbc a,0x99
    ld a,e
    sbc a,0xFD
    ld a,d
    sbc a,0xFF
    jp po,lt27
    xor 0x80
lt27:
    jp m,al19
    ld a,l
    sub 0x67
    ld a,h
    sbc a,0x66
    ld a,e
    sbc a,0x02
    ld a,d
    sbc a,0x00
    jp po,lt28
    xor 0x80
lt28:
    jp m,ao21
    ld hl,0x6666
    ld de,0x0002
    jr ao21
al19:
    ld hl,0x999A
    ld de,0xFFFD
ao21:
    ld a,l
    sub 0xCD
    ld a,h
    sbc a,0xCC
    ld a,e
    sbc a,0xFF
    ld a,d
    sbc a,0xFF
    jp po,lt29
    xor 0x80
lt29:
    jp m,rm23
    ld a,l
    sub 0x34
    ld a,h
    sbc a,0x33
    ld a,e
    sbc a,0x00
    ld a,d
    sbc a,0x00
    jp po,lt30
    xor 0x80
lt30:
    jp m,rd24
    ld bc,1                     ; a > 0.2: +1
    jr rp22
rm23:
    ld bc,-1                    ; a < -0.2: -1
rp22:
    ld (_ld_sc + 46),bc
rd24:
    call _div100
    ld bc,(_ld_sc + 36)
    add hl,bc
    ex de,hl
    ld bc,(_ld_sc + 36 + 2)
    adc hl,bc
    ex de,hl
    ld (_ld_sc + 36),hl
    ld (_ld_sc + 36 + 2),de
    ret

_ls_angles:
; ---- angle 0: t = angle + rate / 100, held within +-MAX_TILT (the rate 0 there)
    ld hl,(_ld_sc + 32)
    ld de,(_ld_sc + 32 + 2)
    call _div100
    ld bc,(_ld_sc + 24)
    add hl,bc
    ex de,hl
    ld bc,(_ld_sc + 24 + 2)
    adc hl,bc
    ex de,hl
    ld a,l
    sub 0xE7
    ld a,h
    sbc a,0xB0
    ld a,e
    sbc a,0xFE
    ld a,d
    sbc a,0xFF
    jp po,lt34
    xor 0x80
lt34:
    jp m,tl31
    ld a,l
    sub 0x1A
    ld a,h
    sbc a,0x4F
    ld a,e
    sbc a,0x01
    ld a,d
    sbc a,0x00
    jp po,lt35
    xor 0x80
lt35:
    jp m,to32
    ld hl,0x4F1A
    ld de,0x0001
    jr th33
tl31:
    ld hl,0xB0E6
    ld de,0xFFFE
th33:
    ld bc,0
    ld (_ld_sc + 32),bc
    ld (_ld_sc + 32 + 2),bc
to32:
    ld (_ld_sc + 24),hl
    ld (_ld_sc + 24 + 2),de
; ---- angle 1: t = angle + rate / 100, held within +-MAX_TILT (the rate 0 there)
    ld hl,(_ld_sc + 36)
    ld de,(_ld_sc + 36 + 2)
    call _div100
    ld bc,(_ld_sc + 28)
    add hl,bc
    ex de,hl
    ld bc,(_ld_sc + 28 + 2)
    adc hl,bc
    ex de,hl
    ld a,l
    sub 0xE7
    ld a,h
    sbc a,0xB0
    ld a,e
    sbc a,0xFE
    ld a,d
    sbc a,0xFF
    jp po,lt39
    xor 0x80
lt39:
    jp m,tl36
    ld a,l
    sub 0x1A
    ld a,h
    sbc a,0x4F
    ld a,e
    sbc a,0x01
    ld a,d
    sbc a,0x00
    jp po,lt40
    xor 0x80
lt40:
    jp m,to37
    ld hl,0x4F1A
    ld de,0x0001
    jr th38
tl36:
    ld hl,0xB0E6
    ld de,0xFFFE
th38:
    ld bc,0
    ld (_ld_sc + 36),bc
    ld (_ld_sc + 36 + 2),bc
to37:
    ld (_ld_sc + 28),hl
    ld (_ld_sc + 28 + 2),de
    ret

_ls_integrate:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld (ls_a),de
    ld hl,(ls_a)                ; v[0] += a[0] / 100
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ex de,hl
    ld bc,(ls_a)
    inc bc
    inc bc
    inc bc
    inc bc
    ld (ls_a),bc
    call _div100
    ld bc,(_ld_sc + 12)
    add hl,bc
    ex de,hl
    ld bc,(_ld_sc + 12 + 2)
    adc hl,bc
    ex de,hl
    ld (_ld_sc + 12),hl
    ld (_ld_sc + 12 + 2),de
    ld hl,(ls_a)                ; v[1] += a[1] / 100
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ex de,hl
    ld bc,(ls_a)
    inc bc
    inc bc
    inc bc
    inc bc
    ld (ls_a),bc
    call _div100
    ld bc,(_ld_sc + 16)
    add hl,bc
    ex de,hl
    ld bc,(_ld_sc + 16 + 2)
    adc hl,bc
    ex de,hl
    ld (_ld_sc + 16),hl
    ld (_ld_sc + 16 + 2),de
    ld hl,(ls_a)                ; v[2] += a[2] / 100
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    inc hl
    ld h,(hl)
    ld l,a
    ex de,hl
    ld bc,(ls_a)
    inc bc
    inc bc
    inc bc
    inc bc
    ld (ls_a),bc
    call _div100
    ld bc,(_ld_sc + 20)
    add hl,bc
    ex de,hl
    ld bc,(_ld_sc + 20 + 2)
    adc hl,bc
    ex de,hl
    ld (_ld_sc + 20),hl
    ld (_ld_sc + 20 + 2),de
    ld hl,(_ld_sc + 12)
    ld de,(_ld_sc + 12 + 2)
    call _div100                ; p[0] += v[0] / 100
    ld bc,(_ld_sc + 0)
    add hl,bc
    ex de,hl
    ld bc,(_ld_sc + 0 + 2)
    adc hl,bc
    ex de,hl
    ld (_ld_sc + 0),hl
    ld (_ld_sc + 0 + 2),de
    ld hl,(_ld_sc + 16)
    ld de,(_ld_sc + 16 + 2)
    call _div100                ; p[1] += v[1] / 100
    ld bc,(_ld_sc + 4)
    add hl,bc
    ex de,hl
    ld bc,(_ld_sc + 4 + 2)
    adc hl,bc
    ex de,hl
    ld (_ld_sc + 4),hl
    ld (_ld_sc + 4 + 2),de
    ld hl,(_ld_sc + 20)
    ld de,(_ld_sc + 20 + 2)
    call _div100                ; p[2] += v[2] / 100
    ld bc,(_ld_sc + 8)
    add hl,bc
    ex de,hl
    ld bc,(_ld_sc + 8 + 2)
    adc hl,bc
    ex de,hl
    ld (_ld_sc + 8),hl
    ld (_ld_sc + 8 + 2),de
    ret
