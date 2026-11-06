; IM 2 interrupt handler for the frame timer (see frame.c / zxgame.h).
; The vector table (257 bytes of 0xF1) lives at 0xF000-0xF100 and the jump
; at 0xF1F1, whatever byte the data bus floats to during the acknowledge.

SECTION code_user

PUBLIC _frame_isr
PUBLIC _frame_ticks
PUBLIC _frame_hook

_frame_isr:
    push af
    ld a,(_frame_ticks)
    inc a
    ld (_frame_ticks),a
    ld a,(_frame_hook + 1)          ; hook set?
    or a
    jr z,isr_done
    push bc
    push de
    push hl
    push ix
    push iy
    ex af,af'
    push af
    exx
    push bc
    push de
    push hl
    ld hl,(_frame_hook)
    call isr_call
    pop hl
    pop de
    pop bc
    exx
    pop af
    ex af,af'
    pop iy
    pop ix
    pop hl
    pop de
    pop bc
isr_done:
    pop af
    ei
    reti
isr_call:
    jp (hl)

SECTION bss_user
_frame_ticks: defb 0
_frame_hook:  defw 0
