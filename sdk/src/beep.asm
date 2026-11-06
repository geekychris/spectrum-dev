; Beeper effects (see zxgame.h). Called with sdcc's standard convention:
; arguments on the stack, return address at (SP).

SECTION code_user

PUBLIC _beep
PUBLIC _beep_noise
EXTERN _snd_border

; void beep(uint16_t period, uint16_t cycles)
; one cycle = two half-periods of (period * 26 + ~40) T-states
_beep:
    ld hl,2
    add hl,sp
    ld e,(hl)
    inc hl
    ld d,(hl)               ; DE = period
    inc hl
    ld c,(hl)
    inc hl
    ld b,(hl)               ; BC = cycles
    ld a,d
    or e
    ret z
    ld a,b
    or c
    ret z
    ld a,(_snd_border)
    and 7
    ld l,a                  ; L = port value (border, speaker off)
beep_cycle:
    ld a,l
    xor 0x10
    ld l,a
    out (0xFE),a            ; speaker on
    push de
beep_d1:
    dec de
    ld a,d
    or e
    jr nz,beep_d1
    pop de
    ld a,l
    xor 0x10
    ld l,a
    out (0xFE),a            ; speaker off
    push de
beep_d2:
    dec de
    ld a,d
    or e
    jr nz,beep_d2
    pop de
    dec bc
    ld a,b
    or c
    jr nz,beep_cycle
    ret

; void beep_noise(uint16_t length): speaker driven by ROM bytes (pseudo noise)
_beep_noise:
    ld hl,2
    add hl,sp
    ld c,(hl)
    inc hl
    ld b,(hl)               ; BC = length
    ld a,b
    or c
    ret z
    ld a,(_snd_border)
    and 7
    ld e,a
    ld hl,0x0100            ; ROM as a noise source
noise_loop:
    ld a,(hl)
    and 0x10
    or e
    out (0xFE),a
    inc hl
    ld a,h
    and 0x3F                ; stay in the ROM
    ld h,a
    ld d,6
noise_d:
    dec d
    jr nz,noise_d
    dec bc
    ld a,b
    or c
    jr nz,noise_loop
    ld a,e
    out (0xFE),a
    ret
