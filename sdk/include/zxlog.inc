; zxlog.inc - debug log for sjasmplus programs (see zxlog.h for the C side).
; INCLUDE it before the first use (it only defines a macro, no code).
;
;   ZXLOG "asm48: ready\n"      ; inline string; preserves all registers (not F)
;
; Writes to ZEsarUX's ZXI debug port (register 1); a no-op on real hardware.

    MACRO ZXLOG text
        push af
        push bc
        push hl
        ld hl,.str
        ld bc,0xCF3B            ; ZXI register select
        ld a,1                  ; register 1 = debug ASCII
        out (c),a
        ld b,0xDF               ; BC = 0xDF3B, ZXI data
.loop:  ld a,(hl)
        or a
        jr z,.done
        out (c),a
        inc hl
        jr .loop
.str:   db text, 0
.done:  pop hl
        pop bc
        pop af
    ENDM
